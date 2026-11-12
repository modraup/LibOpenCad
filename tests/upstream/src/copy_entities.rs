// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, EntityType, Handle, Vector2, Vector3};
use acadrust::objects::{ObjectType, XRecordValue};
use acadrust::xdata::{ExtendedData, XDataValue};
use std::collections::HashMap;

// Associate independently allocated graphs by dictionary names and extension ownership,
// not allocation order. The upstream importer uses a different traversal order.
fn associate(a: &CadDocument, b: &CadDocument, root_a: Handle, root_b: Handle) -> HashMap<Handle,Handle> {
    let mut map=HashMap::from([(root_a,root_b)]);
    let mut pending=vec![(a.extension_dictionary_handle(root_a).unwrap(),b.extension_dictionary_handle(root_b).unwrap())];
    while let Some((ha,hb))=pending.pop() {
        if let Some(previous)=map.insert(ha,hb) {assert_eq!(previous,hb);continue;}
        match (&a.objects[&ha],&b.objects[&hb]) {
            (ObjectType::Dictionary(x),ObjectType::Dictionary(y)) => {
                assert_eq!(x.entries.len(),y.entries.len());
                for (name,h) in &x.entries {
                    let matching=y.entries.iter().find(|(n,_)| n==name).unwrap();
                    pending.push((*h,matching.1));
                }
                assert_eq!(x.xdictionary_handle.is_some(),y.xdictionary_handle.is_some());
                if let (Some(x),Some(y))=(x.xdictionary_handle,y.xdictionary_handle) {pending.push((x,y));}
            }
            (ObjectType::XRecord(x),ObjectType::XRecord(y)) => {
                assert_eq!(x.xdictionary_handle.is_some(),y.xdictionary_handle.is_some());
                if let (Some(x),Some(y))=(x.xdictionary_handle,y.xdictionary_handle) {pending.push((x,y));}
            }
            _=>panic!("copied object type mismatch"),
        }
    }
    assert_eq!(map.len(),7); map
}
fn mapped(h: Handle,map: &HashMap<Handle,Handle>)->Handle {map.get(&h).copied().unwrap_or(h)}
pub(crate) fn rewrite_object(mut object: ObjectType,map: &HashMap<Handle,Handle>)->ObjectType {
    match &mut object {
        ObjectType::Dictionary(d)=> {
            d.handle=mapped(d.handle,map);d.owner=mapped(d.owner,map);
            for (_,h) in &mut d.entries {*h=mapped(*h,map);}
            for h in &mut d.reactors {*h=mapped(*h,map);}
            d.xdictionary_handle=d.xdictionary_handle.map(|h|mapped(h,map));
        }
        ObjectType::XRecord(x)=> {
            x.handle=mapped(x.handle,map);x.owner=mapped(x.owner,map);
            for h in &mut x.reactors {*h=mapped(*h,map);}
            x.xdictionary_handle=x.xdictionary_handle.map(|h|mapped(h,map));
            for e in &mut x.entries {if let XRecordValue::Handle(h)=&mut e.value {*h=mapped(*h,map);}}
            for r in &mut x.object_references {r.handle=mapped(r.handle,map);}
        }
        _=>panic!("unexpected graph object"),
    }
    object
}
pub fn verify(prefix: &str)->acadrust::Result<()> {
    let mut before=DxfReader::from_file(format!("{prefix}-before.dxf"))?.read()?;
    let mut source=before.entities().find(|e|e.common().xdictionary_handle==Some(Handle::new(0x80))).unwrap().common().handle;
    for phase in ["after","return"] {
        let actual=DxfReader::from_file(format!("{prefix}-{phase}.dxf"))?.read()?;
        let copy=actual.entities().find(|e|before.get_entity(e.common().handle).is_none()).unwrap();
        let fresh=copy.common().handle;
        let paper=before.get_entity(source).unwrap().common().owner_handle==Handle::new(0x15);
        assert_eq!(copy.common().owner_handle,Handle::new(if paper{0x18}else{0x15}));
        assert_eq!(copy.common().entity_mode,if paper{Some(1)}else{Some(2)});
        assert_eq!(actual.entities().count(),before.entities().count()+1);
        assert_eq!(actual.objects.len(),before.objects.len()+6);
        assert_eq!(actual.header.handle_seed,before.header.handle_seed+7);
        for e in before.entities() {assert_eq!(actual.get_entity(e.common().handle),Some(e),"source and neighboring entities");}
        for (h,o) in &before.objects {assert_eq!(&actual.objects[h],o,"source and neighboring objects {h:?}");}
        // Exercise the upstream's own public graph exporter/importer, unchanged.
        let graph=before.export_block_graph(source).expect("source extension graph");
        let mut reference=before.clone();
        let mut entity=before.get_entity(source).unwrap().clone();
        entity.common_mut().handle=Handle::NULL;
        entity.common_mut().owner_handle=Handle::NULL;
        entity.common_mut().xdictionary_handle=None;
        entity.common_mut().entity_mode=if paper{Some(1)}else{Some(2)};
        let rust_fresh=if paper {reference.add_paper_space_entity(entity)?} else {reference.add_entity(entity)?};
        reference.import_block_graph(&graph,rust_fresh,&HashMap::new());
        let alignment=associate(&reference,&actual,rust_fresh,fresh);
        for (from,to) in &alignment {
            if *from==rust_fresh {continue;}
            assert_eq!(rewrite_object(reference.objects[from].clone(),&alignment),actual.objects[to],"Rust-imported object {to:?}");
        }
        let source_map=associate(&before,&actual,source,fresh);
        let mut expected=reference.get_entity(rust_fresh).unwrap().clone();
        let common=expected.common_mut();
        common.handle=fresh;
        common.xdictionary_handle=actual.extension_dictionary_handle(fresh);
        for h in &mut common.reactors {*h=mapped(*h,&source_map);}
        let mut xdata=ExtendedData::new();
        for record in common.extended_data.records() {
            let mut record=record.clone();
            for value in &mut record.values {if let XDataValue::Handle(h)=value {*h=mapped(*h,&source_map);}}
            xdata.add_record(record);
        }
        common.extended_data=xdata;
        assert_eq!(&expected,copy,"geometry, text and all common properties");
        assert_eq!(actual.layers,before.layers);assert_eq!(actual.line_types,before.line_types);assert_eq!(actual.text_styles,before.text_styles);
        for block in before.block_records.iter() {
            let mut expected=block.clone();
            if expected.handle==copy.common().owner_handle {expected.entity_handles.push(fresh);}
            assert_eq!(actual.block_records.get(&block.name),Some(&expected));
        }
        before=actual;source=fresh;
    }
    if let EntityType::Ellipse(ellipse)=before.get_entity(source).unwrap() {
        assert_eq!(ellipse.center,Vector3::new(9.,3.,2.));
        assert_eq!(ellipse.major_axis,Vector3::new(4.,3.,0.));assert_eq!(ellipse.minor_axis_ratio,0.6);
        assert_eq!(ellipse.normal,Vector3::new(0.,0.,-1.));assert!(ellipse.is_full());
        let actual=DxfReader::from_file(format!("{prefix}-edited.dxf"))?.read()?;
        let mut expected=before.get_entity(source).unwrap().clone();
        let EntityType::Ellipse(ellipse)=&mut expected else {unreachable!()};
        ellipse.center=Vector3::new(1.,2.,3.);ellipse.major_axis=Vector3::new(6.,0.,2.);
        ellipse.minor_axis_ratio=0.25;ellipse.normal=Vector3::new(0.,1.,0.);
        ellipse.start_parameter=0.125;ellipse.end_parameter=3.5;
        assert!(!ellipse.is_full());
        assert_eq!(actual.entities().count(),before.entities().count());
        for entity in before.entities() {
            let h=entity.common().handle;
            assert_eq!(actual.get_entity(h),Some(if h==source{&expected}else{entity}),"ELLIPSE geometry, radian parameters and common data");
        }
        assert_eq!(actual.objects,before.objects);assert_eq!(actual.block_records,before.block_records);
        assert_eq!(actual.header,before.header);assert_eq!(actual.layers,before.layers);
    }
    if let EntityType::Arc(arc)=before.get_entity(source).unwrap() {
        use std::f64::consts::PI;
        assert_eq!(arc.center,Vector3::new(2.,3.,4.));assert_eq!(arc.radius,5.);
        assert!((arc.start_angle-11.*PI/6.).abs()<1e-12 && (arc.end_angle-PI/6.).abs()<1e-12);
        assert_eq!(arc.normal,Vector3::new(0.,1.,0.));assert_eq!(arc.thickness,0.5);
        let actual=DxfReader::from_file(format!("{prefix}-edited.dxf"))?.read()?;
        let mut expected=before.get_entity(source).unwrap().clone();
        let EntityType::Arc(arc)=&mut expected else {unreachable!()};
        arc.center=Vector3::new(4.,-2.,3.);arc.radius=7.;
        let EntityType::Arc(saved)=actual.get_entity(source).unwrap() else {panic!("edited ARC type")};
        assert!((saved.start_angle-1.5*PI).abs()<1e-12 && (saved.end_angle-PI/4.).abs()<1e-12);
        // Degree/radian conversion can differ in the last bit; all other fields compare exactly.
        arc.start_angle=saved.start_angle;arc.end_angle=saved.end_angle;
        assert_eq!(actual.entities().count(),before.entities().count());
        for entity in before.entities() {
            let h=entity.common().handle;
            assert_eq!(actual.get_entity(h),Some(if h==source{&expected}else{entity}),"edited ARC vs unchanged source and neighbors");
        }
        assert_eq!(actual.objects,before.objects);assert_eq!(actual.block_records,before.block_records);
        assert_eq!(actual.header,before.header);assert_eq!(actual.layers,before.layers);
    }
    if let EntityType::LwPolyline(path)=before.get_entity(source).unwrap() {
        assert!(path.is_closed && path.plinegen);
        assert_eq!(path.vertices.len(),4);
        for (i,v) in path.vertices.iter().enumerate() {
            assert_eq!(v.start_width,i as f64*0.125);
            assert_eq!(v.end_width,i as f64*0.125+0.25);
            assert_eq!(v.vertex_id,70001+i as i32);
        }
        for (phase,closed) in [("edited",true),("open",false)] {
            let actual=DxfReader::from_file(format!("{prefix}-{phase}.dxf"))?.read()?;
            let mut expected=before.get_entity(source).unwrap().clone();
            let EntityType::LwPolyline(path)=&mut expected else {unreachable!()};
            path.vertices[1].location=Vector2::new(10.,1.5);
            path.vertices[0].bulge=-0.5;
            path.vertices[3].bulge=0.75; // Closing segment of the closed copy.
            path.is_closed=closed;
            assert_eq!(actual.entities().count(),before.entities().count());
            for entity in before.entities() {
                let h=entity.common().handle;
                assert_eq!(actual.get_entity(h),Some(if h==source{&expected}else{entity}),
                    "edited copy vs unchanged source and earlier copy");
            }
            assert_eq!(actual.objects,before.objects,"source and cloned extension trees unchanged by geometry edit");
            assert_eq!(actual.block_records,before.block_records);
            assert_eq!(actual.header,before.header);
            assert_eq!(actual.layers,before.layers);
        }
    }
    println!("C++ copies match Rust graph import, with independent handles, internal remapping, target spaces and unchanged sources");
    Ok(())
}
