// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.dim_styles
        .add(acadrust::tables::DimStyle::new("中文标注"))?;
    let model = d.block_records.get("*Model_Space").unwrap().handle;
    let style = d.text_styles.get("Standard").unwrap().handle;
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        let mut t = crate::annotations::tolerance();
        t.normal = n;
        t.text.push_str("^J中文^J末行");
        let h = d.add_entity(EntityType::Tolerance(t))?;
        let mut l = crate::annotations::leader(h);
        l.normal = n;
        d.add_entity(EntityType::Leader(l))?;
        let mut m = crate::mlines::line(Handle::NULL, n.z < 0.);
        m.normal = n;
        m.style_name = "Standard".into();
        m.style_handle = None;
        d.add_entity(EntityType::MLine(m))?;
        for block in [false, true] {
            let mut m = crate::multileaders::definition(model, Handle::NULL, style, block);
            m.style_handle = None;
            m.context.text_normal = n;
            m.context.base_direction = Vector3::new(1., 2., 3.);
            m.context.base_vertical = Vector3::new(-2., 1., 3.);
            d.add_entity(EntityType::MultiLeader(Box::new(m)))?;
        }
    }
    d.layers.add(acadrust::Layer::new("Control"))?;
    d.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
    for e in d.entities_mut() {
        e.common_mut().extended_data.add_record(ExtendedDataRecord {
            application_name: "Geometry".into(),
            values: vec![
                X::Position3D(Vector3::new(1., 2., 3.)),
                X::Direction3D(Vector3::UNIT_Y),
                X::Distance(2.5),
            ],
        });
    }
    let mut controls = Vec::new();
    for kind in ["LEADER", "TOLERANCE", "MLINE", "MULTILEADER"] {
        let mut e = d
            .entities()
            .find(|e| e.as_entity().entity_type() == kind)
            .unwrap()
            .clone();
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        controls.push(e);
    }
    for e in controls {
        d.add_entity(e)?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(point($a.$field,$b.$field);$a.$field=$b.$field;)*}}
macro_rules! numbers{($a:ident,$b:ident,$($field:ident),*)=>{$(near($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Leader(a), EntityType::Leader(b)) => {
            vectors!(
                a,
                b,
                normal,
                horizontal_direction,
                block_offset,
                annotation_offset,
                origin
            );
            numbers!(a, b, text_height, text_width);
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                point(*a, *b);
                *a = *b;
            }
        }
        (EntityType::Tolerance(a), EntityType::Tolerance(b)) => {
            vectors!(a, b, insertion_point, direction, normal);
            numbers!(a, b, text_height, dimension_gap);
        }
        (EntityType::MLine(a), EntityType::MLine(b)) => {
            vectors!(a, b, start_point, normal);
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                vectors!(a, b, position, direction, miter);
            }
        }
        (EntityType::MultiLeader(a), EntityType::MultiLeader(b)) => {
            numbers!(
                a,
                b,
                arrowhead_size,
                text_height,
                dogleg_length,
                block_rotation
            );
            let a = &mut a.context;
            let b = &b.context;
            vectors!(
                a,
                b,
                content_base_point,
                text_location,
                block_content_location,
                base_point,
                text_normal,
                text_direction,
                base_direction,
                base_vertical,
                block_content_scale
            );
            numbers!(
                a,
                b,
                arrowhead_size,
                text_height,
                landing_gap,
                text_width,
                text_boundary_height,
                text_rotation,
                block_rotation
            );
            assert_eq!(a.leader_roots.len(), b.leader_roots.len());
            for (a, b) in a.leader_roots.iter_mut().zip(&b.leader_roots) {
                vectors!(a, b, connection_point, direction);
                numbers!(a, b, landing_distance);
                assert_eq!(a.break_points.len(), b.break_points.len());
                for (a, b) in a.break_points.iter_mut().zip(&b.break_points) {
                    vectors!(a, b, start_point, end_point);
                }
                assert_eq!(a.lines.len(), b.lines.len());
                for (a, b) in a.lines.iter_mut().zip(&b.lines) {
                    assert_eq!(a.points.len(), b.points.len());
                    for (a, b) in a.points.iter_mut().zip(&b.points) {
                        point(*a, *b);
                        *a = *b;
                    }
                }
            }
        }
        _ => panic!("Wrong annotation type"),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 24);
    for step in 0..14 {
        eprintln!("annotation step {step}");
        let t = crate::entity_transform::operation(step);
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" {
                continue;
            }
            if step == 0 {
                e.translate(Vector3::new(3., -4., 5.));
            } else if [5, 6, 7, 11].contains(&step) {
                e.apply_mirror(&t);
            } else {
                e.apply_transform(&t);
            }
        }
        let rows = std::fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        assert_eq!(rows.lines().count(), 24);
        for row in rows.lines() {
            let p: Vec<_> = row.split_whitespace().collect();
            let h = Handle::new(u64::from_str_radix(p[0], 16).unwrap());
            let e = expected.get_entity(h).unwrap();
            let b = if let EntityType::MultiLeader(v) = e {
                let (a, b) = v.bounding_box().unwrap();
                acadrust::BoundingBox3D::new(a, b)
            } else {
                e.as_entity().bounding_box()
            };
            let get = |i: usize| p[i].parse::<f64>().unwrap();
            point(Vector3::new(get(1), get(2), get(3)), b.min);
            point(Vector3::new(get(4), get(5), get(6)), b.max);
            match e {
                EntityType::Leader(l) => {
                    near(get(7), l.length());
                    point(Vector3::new(get(8), get(9), get(10)), l.origin);
                }
                EntityType::Tolerance(v) => {
                    near(get(7), v.line_count() as f64);
                    near(get(8), v.text_height);
                    near(get(9), v.dimension_gap);
                }
                EntityType::MLine(v) => {
                    assert_eq!(get(7) != 0., v.bounding_box().is_some());
                    near(get(8), v.length());
                }
                EntityType::MultiLeader(v) => {
                    near(get(7), v.text_height);
                    let c = &v.context;
                    for (i, p) in [
                        c.text_normal,
                        c.text_location,
                        c.text_direction,
                        c.block_content_location,
                        c.block_content_scale,
                    ]
                    .into_iter()
                    .enumerate()
                    {
                        let n = 8 + i * 3;
                        point(Vector3::new(get(n), get(n + 1), get(n + 2)), p);
                    }
                    for (i, v) in [
                        c.text_rotation,
                        c.text_width,
                        c.text_boundary_height,
                        c.block_rotation,
                    ]
                    .into_iter()
                    .enumerate()
                    {
                        near(get(23 + i), v);
                    }
                }
                _ => panic!(),
            }
        }
        // Project through the unmodified upstream DXF writer/reader. DWG-only values,
        // inactive context content and legacy line-break caches do not persist in DXF.
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(&path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), 24);
        for b in expected.entities() {
            let a = actual.get_entity(b.common().handle).unwrap();
            crate::entity_transform::compare(a, b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.dim_styles, actual.dim_styles);
        assert_eq!(source.line_types, actual.line_types);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
