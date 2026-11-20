// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader,DxfWriter,EntityType,Handle};
use std::io::Cursor;
use acadrust::objects::{ObjectType,XRecordValue};
pub fn verify(prefix:&str)->acadrust::Result<()> {
    for format in ["ascii","binary"] {
        let names=DxfReader::from_file(format!("{prefix}-names-{format}.dxf"))?.read()?;
        for o in names.objects.values(){match o {
            ObjectType::TableStyle(t)=>{assert_eq!(t.data_row_style.text_style_name,"新字体");assert_eq!(t.header_row_style.text_style_name,"新字体");},
            ObjectType::MLineStyle(t)=>for e in &t.elements{assert_eq!(e.linetype,"新线型");},
            ObjectType::Layout(l)=>assert_eq!(l.plot_view_name,"新视图"),_=>{}
        }}
        assert!(names.block_records.get("新块").is_some());

        let owners=DxfReader::from_file(format!("{prefix}-owners-{format}.dxf"))?.read()?;
        assert_eq!(owners.entities_in_block("*Model_Space").count(),2);
        assert_eq!(owners.entities_in_block("*Paper_Space").count(),1);
        assert_eq!(owners.get_entity(Handle::new(0x32)).unwrap().common().owner_handle,Handle::new(0x18));
        let path=owners.entities().find_map(|e|match e{EntityType::Polyline3D(p)=>Some(p),_=>None}).unwrap();
        assert_eq!(path.vertices[1].position,acadrust::Vector3::new(1.,2.,3.));
        let blocks=DxfReader::from_file(format!("{prefix}-block-owners-{format}.dxf"))?.read()?;
        assert_eq!(blocks.entities_in_block("*Model_Space").count(),2);assert_eq!(blocks.entities_in_block("*Paper_Space").count(),1);
        let from_block=blocks.get_entity(path.common.handle).unwrap();
        assert_eq!(from_block,&EntityType::Polyline3D(path.clone()));
        let before=DxfReader::from_file(format!("{prefix}-before-{format}.dxf"))?.read()?;
        let after=DxfReader::from_file(format!("{prefix}-after-{format}.dxf"))?.read()?;
        let original=Handle::new(0x32);let line=Handle::new(0x33);
        assert!(before.xrecord(original,"中文数据").is_some());
        let source=before.get_entity(original).unwrap();let copy=after.entities().find(|e|matches!(e,EntityType::Arc(_))).unwrap();
        let (EntityType::Arc(src),EntityType::Arc(dst))=(source,copy)else{panic!()};
        assert_eq!(src.center,dst.center);assert_eq!((src.radius,src.start_angle,src.end_angle),(dst.radius,dst.start_angle,dst.end_angle));
        assert_eq!(copy.common().owner_handle,Handle::new(0x18));
        let original_data=before.xrecord(original,"中文数据").unwrap();let data=after.xrecord(copy.common().handle,"中文数据").unwrap();
        assert_eq!(original_data.entries.len(),data.entries.len());
        for(code,expected) in [(1,XRecordValue::String("文字 ^J".into())),(105,XRecordValue::String("BEEF".into())),(320,XRecordValue::String("CAFE".into())),(1001,XRecordValue::String("literal-app".into())),(310,XRecordValue::Chunk(vec![0,255,18,52])),(160,XRecordValue::Int64(i64::MIN)),(290,XRecordValue::Bool(true))] {
            assert_eq!(data.get_first_by_code(code).unwrap().value,expected);
        }
        assert_eq!(data.get_first_by_code(340).unwrap().value.as_handle(),Some(copy.common().handle));
        assert_eq!(data.get_first_by_code(331).unwrap().value.as_handle(),Some(line));
        // The transport conversion must preserve all typed XRECORD values and its literal strings.
        let ref_before=before.objects.values().filter(|o|matches!(o,ObjectType::XRecord(_))).count();
        let ref_after=after.objects.values().filter(|o|matches!(o,ObjectType::XRecord(_))).count();assert_eq!(ref_before,ref_after);
        let mut reference=before.clone();reference.remove_entity(original).unwrap();
        for h in before.objects.keys().filter(|h|!reference.objects.contains_key(h)){assert!(!after.objects.contains_key(h));}
        let mut expected=before.get_entity(line).unwrap().clone();expected.common_mut().reactors.retain(|h|*h!=original);
        assert_eq!(after.get_entity(line),Some(&expected));assert!(after.get_entity(original).is_none());
        assert_eq!(after.entities_in_block("*Model_Space").count(),1);assert_eq!(after.entities_in_block("*Paper_Space").count(),1);
    }
    let a=DxfReader::from_file(format!("{prefix}-after-ascii.dxf"))?.read()?;
    let b=DxfReader::from_file(format!("{prefix}-after-binary.dxf"))?.read()?;
    for(h,object) in &a.objects {if h.value()>=0x31 {assert_eq!(Some(object),b.objects.get(h));}}
    assert_eq!(a.entities().collect::<Vec<_>>(),b.entities().collect::<Vec<_>>());
    // Existing TABLESTYLE/LAYOUT retain transport-specific numeric whitespace in raw codes.
    let a=DxfReader::from_reader(Cursor::new(DxfWriter::new_binary(&a).write_to_vec()?))?.read()?;
    let b=DxfReader::from_reader(Cursor::new(DxfWriter::new_binary(&b).write_to_vec()?))?.read()?;
    assert_eq!(a.objects,b.objects);
    println!("Rust verified repaired references, new dictionary trees and cascading deletion in both encodings");Ok(())
}
