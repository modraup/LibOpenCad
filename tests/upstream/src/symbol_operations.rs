// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, EntityType, Vector3};
pub fn verify(prefix:&str)->acadrust::Result<()> {
    for suffix in ["-ascii.dxf","-binary.dxf"] {
        let doc=DxfReader::from_file(format!("{prefix}{suffix}"))?.read()?;
        assert!(doc.layers.get("几何").is_some());assert!(doc.line_types.get("虚线").is_some());
        let text=doc.text_styles.get("已改名").unwrap();assert_eq!(text.font_file,"txt");assert_eq!((text.height,text.width_factor,text.last_height),(0.,1.,2.5));
        let custom=doc.text_styles.get("字体").unwrap();assert_eq!(custom.font_file,"simplex.shx");assert_eq!((custom.height,custom.width_factor),(3.5,0.8));
        let dim=doc.dim_styles.get("已改名").unwrap();assert_eq!(dim.dimtxt,0.18);assert_eq!(dim.dimtxsty_handle,doc.text_styles.get("Standard").unwrap().handle);
        assert!(doc.app_ids.get("已改名").is_some());
        let ucs=doc.ucss.get("已改名").unwrap();assert_eq!(ucs.origin,Vector3::ZERO);assert_eq!(ucs.x_axis,Vector3::UNIT_X);assert_eq!(ucs.y_axis,Vector3::UNIT_Y);
        assert_eq!(doc.ucss.get("坐标").unwrap().origin,Vector3::new(3.,4.,5.));
        let view=doc.views.get("已改名").unwrap();assert_eq!(view.direction,Vector3::UNIT_Z);assert_eq!((view.height,view.width,view.lens_length),(1.,1.,50.));
        let vp=doc.vports.get("已改名").unwrap();assert_eq!((vp.view_height,vp.aspect_ratio,vp.lens_length),(10.,1.,50.));
        let block=doc.block_records.get("已改名").unwrap();assert_eq!(block.units,4);assert!(block.explodable);assert!(!block.scale_uniformly);assert!(doc.entities_in_block("已改名").next().is_none());
        assert_eq!((doc.text_styles.len(),doc.dim_styles.len(),doc.app_ids.len(),doc.ucss.len(),doc.views.len(),doc.vports.len(),doc.block_records.len()),(3,2,5,2,1,2,3));
        let state=doc.objects.values().find_map(|o|match o {acadrust::objects::ObjectType::XRecord(x) if x.name=="方案"=>Some(x),_=>None}).unwrap();
        assert_eq!(state.get_first_by_code(302).unwrap().value.as_string(),Some("几何"));
        assert_eq!(doc.entities().count(),1);let EntityType::Line(line)=doc.entities().next().unwrap() else{panic!()};
        assert_eq!(line.common.layer,"几何");assert_eq!(line.common.linetype,"虚线");
        assert!(doc.layers.get("临时").is_none());assert!(doc.block_records.get("临时").is_none());
    }
    println!("Rust verified nine symbol tables, defaults, property overrides and name references");Ok(())
}
