// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::{
    GeoData, GeoDataMeshFace, GeoDataMeshPoint, ObjectType, VisualStyle, VisualStyleProperty,
    VisualStylePropertyValue,
};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
fn insert(d: &mut CadDocument, name: &str, mut o: ObjectType) {
    let root = d.header.named_objects_dict_handle;
    let h = d.allocate_handle();
    match &mut o {
        ObjectType::VisualStyle(v) => {
            v.handle = h;
            v.owner = root;
        }
        ObjectType::GeoData(v) => {
            v.handle = h;
            v.owner = root;
        }
        _ => panic!(),
    }
    if let ObjectType::Dictionary(dict) = d.objects.get_mut(&root).unwrap() {
        dict.add_entry(name, h);
    }
    d.objects.insert(h, o);
}
fn named(d: &CadDocument, name: &str) -> Handle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::Dictionary(d) = o {
                d.get(name)
            } else {
                None
            }
        })
        .unwrap()
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for name in ["视觉", "邻居视觉"] {
        let mut v = VisualStyle::new();
        v.description = name.into();
        v.face_lighting_model = 2;
        v.properties = vec![VisualStyleProperty {
            value: VisualStylePropertyValue::Long(2),
            enabled: 3,
        }];
        insert(&mut d, name, ObjectType::VisualStyle(v));
    }
    for version in [1, 2, 3] {
        let mut v = GeoData::new();
        v.version = version;
        v.coordinate_system_definition = format!("<xml>\n{}^J\t</xml>", "坐标系".repeat(100));
        v.coordinate_system_datum = "基准".into();
        v.coordinate_system_wkt = "WKT".into();
        v.design_point = Vector3::new(1., 2., 3.);
        v.reference_point = Vector3::new(4., 5., 6.);
        v.north_direction = Vector2::new(0., 1.);
        v.up_direction = Vector3::UNIT_Z;
        v.mesh_points = vec![
            GeoDataMeshPoint {
                source: Vector2::new(1., 2.),
                destination: Vector2::new(3., 4.),
            },
            GeoDataMeshPoint {
                source: Vector2::new(5., 6.),
                destination: Vector2::new(7., 8.),
            },
        ];
        v.mesh_faces = vec![GeoDataMeshFace {
            first: 0,
            second: 1,
            third: 0,
        }];
        v.civil_reference_point1 = Vector2::new(10., 11.);
        v.civil_unknown1 = 12;
        insert(&mut d, &format!("坐标{version}"), ObjectType::GeoData(v));
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for name in ["视觉", "邻居视觉", "坐标1", "坐标2", "坐标3"] {
        let h = named(&src, name);
        assert_eq!(round.objects[&h], src.objects[&h], "{name}");
    }
    let edited = read("edited")?;
    if let ObjectType::VisualStyle(v) = &edited.objects[&named(&edited, "视觉")] {
        assert_eq!(v.properties[4].value, VisualStylePropertyValue::Double(0.9));
        assert_eq!(v.properties[4].enabled, 2);
    } else {
        panic!()
    }
    assert_eq!(
        edited.objects[&named(&edited, "邻居视觉")],
        src.objects[&named(&src, "邻居视觉")]
    );
    if let ObjectType::GeoData(v) = &edited.objects[&named(&edited, "坐标2")] {
        assert_eq!(v.mesh_points[0].destination.x, 99.);
        assert_eq!(v.mesh_faces[0].third, 1);
    } else {
        panic!()
    }
    let fresh = read("fresh")?;
    if let ObjectType::VisualStyle(v) = &fresh.objects[&named(&fresh, "新视觉")] {
        assert_eq!(v.properties.len(), 58);
    } else {
        panic!()
    }
    let mut count = 0;
    for e in fresh.entities() {
        match e {
            EntityType::LwPolyline(v) => {
                assert_eq!(v.vertices.len(), 4);
                assert_eq!(v.vertices[0].vertex_id, 7);
                assert_eq!(v.vertices[0].start_width, 2.);
                assert_eq!(v.vertices[0].end_width, 3.);
                assert_eq!(v.thickness, 4.);
                assert_eq!(v.constant_width, 5.);
            }
            EntityType::Polyline2D(v) => {
                assert_eq!(v.elevation, 6.);
                assert_eq!(v.thickness, 7.);
                assert_eq!(v.vertices[0].id, 0); // Pinned Rust drops traditional VERTEX group 91; native test checks 8.
                assert!((v.vertices[0].curve_tangent - 0.25).abs() < 1e-12);
                assert_eq!(v.start_width, 0.); // Rust projects explicit widths onto vertices.
            }
            EntityType::Polyline3D(v) => {
                assert_eq!(v.vertices[0].position, Vector3::new(1., 2., 3.));
                assert_eq!(v.elevation, 12.);
                assert_eq!(v.normal, Vector3::UNIT_Y);
                assert_eq!(v.default_start_width, 2.);
                assert_eq!(v.default_end_width, 3.);
                assert_eq!(v.smooth_m_density, 0);
                assert_eq!(v.smooth_n_density, 0); // Pinned DXF reader omits 3D smoothing density; native checks retain 4/5.
            }
            EntityType::PolygonMesh(v) => {
                assert_eq!(v.m_smooth_density, 3);
                assert_eq!(v.n_smooth_density, 4);
                assert_eq!(v.elevation, 13.);
                assert_eq!(v.normal, Vector3::UNIT_Y);
            }
            EntityType::PolyfaceMesh(v) => {
                assert_eq!(v.elevation, 14.);
                assert_eq!(v.thickness, 0.); // Pinned polyface reader drops header thickness and vertex widths/bulge.
                assert_eq!(v.vertices[0].location, Vector3::new(7., 8., 9.));
                assert_eq!(v.vertices[0].bulge, 0.);
                assert_eq!(v.vertices[0].start_width, 0.);
                assert_eq!(v.vertices[0].end_width, 0.);
                assert_eq!(
                    v.faces[0].color,
                    Some(acadrust::types::Color::from_true_color_value(0x123456))
                );
            }
            _ => panic!(),
        }
        count += 1;
    }
    assert_eq!(count, 5);
    println!("Rust verified visual properties, GEODATA mesh/Civil3D/chunked Unicode and supported polyline metadata; native tests cover pinned reader omissions");
    Ok(())
}
