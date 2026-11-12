// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument,DxfReader,EntityType,Handle,Vector3};
use acadrust::objects::ObjectType;
use std::collections::HashMap;
type Record=Vec<(i32,String)>;
fn h(s:&str)->Handle {Handle::new(u64::from_str_radix(s.trim(),16).unwrap())}
fn raw(path:&str)->acadrust::Result<HashMap<Handle,Record>> {
    let mut records=Vec::<Record>::new();
    for pair in crate::remove_polylines::raw_string_pairs(path)? {
        if pair.0==0 {records.push(Vec::new());} records.last_mut().unwrap().push(pair);
    }
    Ok(records.into_iter().filter_map(|r| {
        if r[0].1=="SECTION" {return None;}
        let id=r.iter().take_while(|p|p.0!=100).find(|p|p.0==5).map(|p|h(&p.1));
        id.map(|id|(id,r))
    }).collect())
}
fn extension(r:&Record)->Option<Handle> {
    r.windows(2).find(|p|p[0]==(102,"{ACAD_XDICTIONARY".into())).map(|p|{assert_eq!(p[1].0,360);h(&p[1].1)})
}
fn sequence(path:&str,root:Handle)->acadrust::Result<Vec<(String,u64,u64)>> {
    let data=crate::remove_polylines::raw_sequences(path)?;
    let start=data.iter().position(|r|r.0=="POLYLINE" && r.1==root.value()).unwrap();
    let end=(start+1..data.len()).find(|&i|data[i].0=="SEQEND").unwrap();
    Ok(data[start..=end].to_vec())
}
fn associate(a:&CadDocument,b:&CadDocument,ra:&HashMap<Handle,Record>,rb:&HashMap<Handle,Record>,map:&mut HashMap<Handle,Handle>) {
    let mut pending=Vec::new();
    for (from,to) in map.iter() {
        assert_eq!(extension(&ra[from]).is_some(),extension(&rb[to]).is_some());
        if let(Some(x),Some(y))=(extension(&ra[from]),extension(&rb[to])) {pending.push((x,y));}
    }
    while let Some((from,to))=pending.pop() {
        assert!(map.insert(from,to).is_none(),"shared object in fixture tree");
        match (&a.objects[&from],&b.objects[&to]) {
            (ObjectType::Dictionary(x),ObjectType::Dictionary(y))=> {
                assert_eq!(x.entries.len(),y.entries.len());
                for(name,h)in &x.entries {pending.push((*h,y.entries.iter().find(|(n,_)|n==name).unwrap().1));}
                if let(Some(x),Some(y))=(x.xdictionary_handle,y.xdictionary_handle){pending.push((x,y));}
            }
            (ObjectType::XRecord(x),ObjectType::XRecord(y))=> {
                assert_eq!(x.xdictionary_handle.is_some(),y.xdictionary_handle.is_some());
                if let(Some(x),Some(y))=(x.xdictionary_handle,y.xdictionary_handle){pending.push((x,y));}
            }
            _=>panic!("copy object kind"),
        }
    }
}
pub fn verify(prefix:&str,kind:&str)->acadrust::Result<()> {
    let spatial=kind=="polyline3d";let mesh=kind=="polygon_mesh";let polyface=kind=="polyface_mesh";
    let sequence_size=if polyface{11}else if mesh{14}else{6};
    let object_count=if polyface{14}else{10};let group_size=sequence_size+object_count;
    let mut input=format!("{prefix}-before.dxf");let mut before=DxfReader::from_file(&input)?.read()?;
    let mut source=before.entities().find(|e|match e {EntityType::Polyline3D(p)=>spatial && p.flags.closed,EntityType::Polyline2D(p)=>kind=="polyline2d" && p.is_closed(),EntityType::PolygonMesh(p)=>mesh && p.m_vertex_count==3,EntityType::PolyfaceMesh(_)=>polyface,_=>false}).unwrap().common().handle;
    let binary=std::fs::read(&input)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0");
    for(phase,paper)in [("after",true),("return",false)] {
        let output=format!("{prefix}-{phase}.dxf");let actual=DxfReader::from_file(&output)?.read()?;
        let copied=actual.entities().find(|e|before.get_entity(e.common().handle).is_none()).unwrap();let fresh=copied.common().handle;
        let old=sequence(&input,source)?;let new=sequence(&output,fresh)?;
        assert_eq!(old.len(),sequence_size);assert_eq!(new.len(),sequence_size);
        let mut map=HashMap::new();
        for(i,(a,b))in old.iter().zip(&new).enumerate(){
            assert_eq!(a.0,b.0);assert!(map.insert(Handle::new(a.1),Handle::new(b.1)).is_none());
            assert_eq!(b.2,if i==0{if paper{0x18}else{0x15}}else{fresh.value()});
            assert!(b.1>=before.header.handle_seed);
        }
        let ra=raw(&input)?;let rb=raw(&output)?;associate(&before,&actual,&ra,&rb,&mut map);
        assert_eq!(map.len(),group_size);assert_eq!(map.values().copied().collect::<std::collections::HashSet<_>>().len(),group_size);
        assert_eq!(actual.header.handle_seed,before.header.handle_seed+group_size as u64);
        assert_eq!(actual.objects.len(),before.objects.len()+object_count);
        // Rust omits Polyface child common data, unclassified vertices and SEQEND; scan raw records as well.
        // Compare their ordered raw records with the upstream group-code layout too.
        for(a,b)in old.iter().zip(&new){
            let mut expected=ra[&Handle::new(a.1)].clone();let mut depth=0;
            for(code,value)in &mut expected {
                if *code==102 {if value=="}"{depth-=1;}else if value.starts_with('{'){depth+=1;}}
                if *code==330 && depth==0 { *value=format!("{:X}",b.2); }
                else if matches!(*code,5|330|340|350|360|1005) {
                    if let Some(to)=map.get(&h(value)){*value=format!("{:X}",to.value());}
                }
                else if *code==67 {*value=if binary{String::new()}else{if paper{"1"}else{"0"}.into()};}
                else if *code==410 {*value=if paper{"Layout1"}else{"Model"}.into();}
            }
            assert_eq!(rb[&Handle::new(b.1)],expected,"raw sequence handles, order, metadata and ownership");
        }
        for(from,to)in &map {if let Some(object)=before.objects.get(from){
            assert_eq!(actual.objects[to],crate::copy_entities::rewrite_object(object.clone(),&map),"cloned object payload and references");
        }}
        for(id,r)in &ra {assert_eq!(rb.get(id),Some(r),"original raw record {id:?}");}
        for(id,o)in &before.objects {assert_eq!(actual.objects.get(id),Some(o));}
        for e in before.entities(){assert_eq!(actual.get_entity(e.common().handle),Some(e));}
        let mut expected=before.get_entity(source).unwrap().clone();let c=expected.common_mut();
        c.handle=fresh;c.owner_handle=Handle::NULL;c.entity_mode=Some(if paper{1}else{2});
        c.xdictionary_handle=c.xdictionary_handle.map(|h|map[&h]);
        for h in &mut c.reactors {if let Some(to)=map.get(h){*h=*to;}}
        if let EntityType::Polyline3D(p)=&mut expected {
            for v in &mut p.vertices {v.handle=map[&v.handle];}
        }
        if let EntityType::PolygonMesh(p)=&mut expected {
            for v in &mut p.vertices {v.common.handle=map[&v.common.handle];v.common.owner_handle=fresh;}
        }
        let mut reference=before.clone();if paper{reference.add_paper_space_entity(expected)?;}else{reference.add_entity(expected)?;}
        assert_eq!(actual.get_entity(fresh),reference.get_entity(fresh),"ordered geometry, child handles, layers, flags and parent common data");
        assert_eq!(actual.entities().count(),before.entities().count()+1);
        assert_eq!(actual.block_records,reference.block_records);
        assert_eq!(actual.layers,before.layers);assert_eq!(actual.line_types,before.line_types);
        before=actual;input=output;source=fresh;
    }
    let actual=DxfReader::from_file(format!("{prefix}-edited.dxf"))?.read()?;
    let mut expected=before.get_entity(source).unwrap().clone();
    match &mut expected {
        EntityType::PolyfaceMesh(path) => {
            assert!(polyface);assert_eq!((path.vertices.len(),path.faces.len()),(5,3));
            let points=[(0.,0.,0.),(4.,0.,0.),(4.,4.,0.),(0.,4.,0.),(2.,2.,3.)];
            for(v,(x,y,z))in path.vertices.iter().zip(points) {assert_eq!(v.location,Vector3::new(x,y,z));}
            assert_eq!([path.faces[0].index1,path.faces[0].index2,path.faces[0].index3,path.faces[0].index4],[1,-2,5,0]);
            assert_eq!([path.faces[1].index1,path.faces[1].index2,path.faces[1].index3,path.faces[1].index4],[-1,2,3,4]);
            [path.faces[0].index1,path.faces[0].index2,path.faces[0].index3]=[-1,3,-5];
            [path.faces[1].index1,path.faces[1].index2,path.faces[1].index3,path.faces[1].index4]=[2,-3,4,-1];
            let EntityType::PolyfaceMesh(saved)=actual.get_entity(source).unwrap() else {panic!("saved polyface")};
            assert!(saved.faces[0].is_triangle() && saved.faces[1].is_quad() && saved.faces[2].is_triangle());
            assert_eq!(saved.faces[0].vertex_indices(),[1,3,5]);
            assert_eq!(saved.faces[1].vertex_indices(),[2,3,4,1]);
            assert_eq!(saved.faces[2].vertex_indices(),[3,4,5]);
            for(face,hidden)in saved.faces.iter().zip([[true,false,true,false],[false,true,false,true],[false,false,true,false]]) {
                assert_eq!([face.is_edge1_invisible(),face.is_edge2_invisible(),face.is_edge3_invisible(),face.is_edge4_invisible()],hidden);
            }
        }
        EntityType::PolygonMesh(path) => {
            assert!(mesh);assert_eq!((path.m_vertex_count,path.n_vertex_count),(3,4));
            assert!(!path.is_closed_m() && path.is_closed_n());assert_eq!(path.vertices.len(),12);
            for(i,v)in path.vertices.iter().enumerate() {
                let m=(i/4)as f64;let n=(i%4)as f64;
                assert_eq!(v.location,Vector3::new(m,n,10.*m-n));
                assert_eq!(v.common.owner_handle,source);assert_eq!(v.flags,64);
                assert_eq!(v.common.layer,if i==6{"测点"}else{"网格"});
            }
            assert_eq!((path.m_smooth_density,path.n_smooth_density),(4,6));
            assert_eq!(path.smooth_type,acadrust::entities::SurfaceSmoothType::Cubic);
            path.vertices[6].location=Vector3::new(1.5,2.25,-7.);
        }
        EntityType::Polyline3D(path) => {
            assert!(spatial && path.flags.closed);assert_eq!(path.vertices.len(),4);
            let positions=[(0.,0.,-2.),(8.,0.,3.),(8.,6.,7.5),(0.,6.,1.)];
            for(v,(x,y,z))in path.vertices.iter().zip(positions) {
                assert_eq!(v.position,Vector3::new(x,y,z));
            }
            path.vertices[1].position=Vector3::new(10.,1.5,-6.25);
        }
        EntityType::Polyline2D(path) => {
            assert!(!spatial && path.is_closed());assert_eq!(path.vertices.len(),4);
            path.vertices[1].location=Vector3::new(10.,1.5,0.);
            path.vertices[0].start_width=0.;path.vertices[0].end_width=1.25;
            path.vertices[2].start_width=2.5;path.vertices[2].end_width=3.5;path.vertices[3].bulge=0.75;
        }
        _=>panic!("copied path type"),
    }
    for e in before.entities(){assert_eq!(actual.get_entity(e.common().handle),Some(if e.common().handle==source{&expected}else{e}));}
    assert_eq!(actual.entities().count(),4);assert_eq!(actual.objects,before.objects);
    assert_eq!(actual.block_records,before.block_records);assert_eq!(actual.header,before.header);
    assert_eq!(crate::remove_polylines::raw_sequences(&format!("{prefix}-edited.dxf"))?,crate::remove_polylines::raw_sequences(&input)?);
    println!("{} POLYLINE group copies preserve ordered geometry; fresh child/object ownership and source isolation verified",kind);Ok(())
}
