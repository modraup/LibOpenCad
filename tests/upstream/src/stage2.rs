// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, EntityType, Vector3};
fn near(a:f64,b:f64){ assert!((a-b).abs()<1e-12,"{a} != {b}"); }
pub fn verify(prefix:&str)->acadrust::Result<()> {
    let mut reference=None;
    for suffix in ["-ascii.dxf","-binary.dxf","-cycled-ascii.dxf","-cycled-binary.dxf"] {
        let doc=DxfReader::from_file(format!("{prefix}{suffix}"))?.read()?;
        assert_eq!(doc.entities().count(),20);
        assert_eq!(doc.layers.len(),2);
        let points=vec![Vector3::new(0.,0.,1.),Vector3::new(8.,0.,2.),Vector3::new(4.,6.,3.),Vector3::new(0.,6.,4.)];
        for space in ["*Model_Space","*Paper_Space"] {
            let entities=doc.entities_in_block(space).collect::<Vec<_>>(); assert_eq!(entities.len(),10);
            let mut seen=[0;10];
            for e in entities {
                assert_eq!(e.common().layer,"几何");
                let kind=match e {
                    EntityType::Line(e)=>{assert_eq!(e.start,Vector3::new(1.,2.,3.));assert_eq!(e.end,Vector3::new(4.,5.,6.));0},
                    EntityType::Circle(e)=>{assert_eq!(e.center,Vector3::new(2.,3.,4.));assert_eq!(e.radius,5.);1},
                    EntityType::Text(e)=>{assert_eq!(e.value,"中文 ^J");assert_eq!(e.insertion_point,Vector3::new(3.,4.,5.));assert_eq!(e.height,2.5);2},
                    EntityType::Arc(e)=>{assert_eq!(e.center,Vector3::new(2.,3.,4.));assert_eq!(e.radius,5.);near(e.start_angle,0.25);near(e.end_angle,4.5);3},
                    EntityType::Ellipse(e)=>{assert_eq!(e.center,Vector3::new(3.,4.,5.));assert_eq!(e.major_axis,Vector3::new(6.,0.,2.));assert_eq!(e.minor_axis_ratio,0.5);assert_eq!((e.start_parameter,e.end_parameter),(0.125,3.5));assert_eq!(e.normal,Vector3::new(0.,1.,0.));4},
                    EntityType::LwPolyline(e)=>{assert!(e.is_closed);assert_eq!(e.vertices.len(),3);for(i,v)in e.vertices.iter().enumerate(){let(x,y,b,s,t)= [(0.,0.,0.25,1.,2.),(8.,0.,0.,2.,3.),(4.,6.,-0.5,3.,1.)][i];assert_eq!((v.location.x,v.location.y,v.bulge,v.start_width,v.end_width),(x,y,b,s,t));}5},
                    EntityType::Polyline2D(e)=>{assert!(e.is_closed());assert_eq!(e.elevation,2.);assert_eq!(e.vertices.len(),3);for(i,v)in e.vertices.iter().enumerate(){let(x,y,b,s,t)=[(0.,0.,0.25,1.,2.),(8.,0.,0.,2.,3.),(4.,6.,-0.5,3.,1.)][i];assert_eq!((v.location.x,v.location.y,v.bulge,v.start_width,v.end_width),(x,y,b,s,t));}6},
                    EntityType::Polyline3D(e)=>{assert!(e.is_closed());assert_eq!(e.vertices.iter().map(|v|v.position).collect::<Vec<_>>(),points);7},
                    EntityType::PolygonMesh(e)=>{assert_eq!((e.m_vertex_count,e.n_vertex_count),(2,2));assert!(e.is_closed_m());assert!(!e.is_closed_n());assert_eq!(e.vertices.iter().map(|v|v.location).collect::<Vec<_>>(),points);8},
                    EntityType::PolyfaceMesh(e)=>{assert_eq!(e.vertices.iter().map(|v|v.location).collect::<Vec<_>>(),points);assert_eq!(e.faces.len(),2);assert_eq!((e.faces[0].index1,e.faces[0].index2,e.faces[0].index3,e.faces[0].index4),(1,-2,3,0));assert_eq!((e.faces[1].index1,e.faces[1].index2,e.faces[1].index3,e.faces[1].index4),(1,2,-3,4));9},
                    _=>panic!("unexpected entity")
                };seen[kind]+=1;
            } assert_eq!(seen,[1;10]);
        }
        let all=doc.entities().cloned().collect::<Vec<_>>();
        if let Some(expected)=&reference { assert_eq!(&all,expected,"transport or copy/delete changed original data"); } else {reference=Some(all);}
    }
    println!("Rust verified 10 entity types in both spaces, geometry and encoding/copy/delete preservation");Ok(())
}
