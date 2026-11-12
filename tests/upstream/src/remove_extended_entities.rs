// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, EntityType, Handle};
use acadrust::objects::ObjectType;
use std::collections::HashSet;

pub fn verify(prefix: &str, space: &str, kind: &str)->acadrust::Result<()> {
    let matches_kind=|e:&EntityType|match e {
        EntityType::Arc(_)=>kind=="arc",EntityType::LwPolyline(_)=>kind=="lwpolyline",
        EntityType::Ellipse(_)=>kind=="ellipse",_=>false,
    };
    let before=DxfReader::from_file(format!("{prefix}-before.dxf"))?.read()?;
    let actual=DxfReader::from_file(format!("{prefix}-after.dxf"))?.read()?;
    let owner=Handle::new(if space=="paper"{0x18}else{0x15});
    let root=before.entities().filter(|e|matches_kind(e) && e.common().owner_handle==owner)
        .map(|e|e.common().handle).max_by_key(|h|h.value()).unwrap();
    let mut reference=before.clone();
    assert!(matches_kind(&reference.remove_entity(root).expect("removed entity")));
    let mut removed: HashSet<_>=before.objects.keys().filter(|h|!reference.objects.contains_key(h)).copied().collect();
    assert_eq!(removed.len(),6,"upstream cascades six extension objects"); removed.insert(root);
    assert_eq!(actual.entities().count(),before.entities().count()-1);
    let mut paths=0;
    for entity in before.entities() {
        let h=entity.common().handle;
        if h==root {assert!(actual.get_entity(h).is_none());continue;}
        let mut expected=entity.clone();
        expected.common_mut().reactors.retain(|h|!removed.contains(h));
        assert_eq!(actual.get_entity(h),Some(&expected),"surviving complete geometry and common data");
        if matches_kind(entity) {
            paths+=1;
            match entity {
                EntityType::LwPolyline(path)=>{
                    assert!(path.is_closed && path.plinegen);assert_eq!(path.vertices.len(),4);
                }
                EntityType::Arc(arc)=>{
                    assert_eq!(arc.normal,acadrust::Vector3::new(0.,1.,0.));assert_eq!(arc.thickness,0.5);
                    assert!(arc.start_angle>arc.end_angle,"wraparound ARC remains intact");
                }
                EntityType::Ellipse(ellipse)=>{
                    if ellipse.is_full() {
                        assert_eq!(ellipse.center,acadrust::Vector3::new(9.,3.,2.));
                        assert_eq!(ellipse.major_axis,acadrust::Vector3::new(4.,3.,0.));
                        assert_eq!(ellipse.minor_axis_ratio,0.6);
                        assert_eq!(ellipse.normal,acadrust::Vector3::new(0.,0.,-1.));
                    } else {
                        assert_eq!(ellipse.center,acadrust::Vector3::new(1.,2.,3.));
                        assert_eq!(ellipse.major_axis,acadrust::Vector3::new(6.,0.,2.));
                        assert_eq!(ellipse.minor_axis_ratio,0.25);
                        assert_eq!((ellipse.start_parameter,ellipse.end_parameter),(0.125,3.5));
                        assert_eq!(ellipse.normal,acadrust::Vector3::new(0.,1.,0.));
                    }
                }
                _=>unreachable!(),
            }
        }
    }
    assert_eq!(paths,2);
    for object in reference.objects.values_mut() {
        if let ObjectType::Dictionary(d)=object {d.reactors.retain(|h|!removed.contains(h));}
    }
    assert_eq!(actual.objects,reference.objects,"source and surviving copied dictionaries/XRECORDs, with only targeted reactors removed");
    for h in &removed {assert!(!actual.objects.contains_key(h));}
    assert_eq!(actual.header,before.header);
    assert_eq!(actual.layers,before.layers);
    assert_eq!(actual.line_types,before.line_types);
    assert_eq!(actual.text_styles,before.text_styles);
    for block in before.block_records.iter() {
        let mut expected=block.clone();expected.entity_handles.retain(|h|*h!=root);
        assert_eq!(actual.block_records.get(&block.name),Some(&expected));
        assert_eq!(actual.entities_in_block(&block.name).map(|e|e.common().handle).collect::<Vec<_>>(),expected.entity_handles);
    }
    println!("{kind} deletion and extension cascade match Rust; source, other copy and dictionary payloads preserved");
    Ok(())
}
