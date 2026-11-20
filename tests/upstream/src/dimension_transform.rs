// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::dimension::Dimension;
use acadrust::{CadDocument, DxfReader, DxfWriter, Entity, EntityType, Handle, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.dim_styles
        .add(acadrust::tables::DimStyle::new("中文标注"))?;
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        for alternate in [false, true] {
            for kind in 0..9 {
                let mut d = crate::dimensions::definition(kind);
                d.base_mut().normal = n;
                if alternate {
                    d.base_mut().text_rotation = 0.;
                    match &mut d {
                        Dimension::Linear(g) => g.ext_line_rotation = 0.,
                        Dimension::Ordinate(g) => g.is_ordinate_type_x = true,
                        Dimension::Angular2Ln(g) => {
                            g.first_point = Vector3::ZERO;
                            g.second_point = Vector3::UNIT_X;
                            g.angle_vertex = Vector3::ZERO;
                            g.definition_point = Vector3::UNIT_Y;
                            g.dimension_arc = Vector3::new(-1., 1., 0.);
                        }
                        Dimension::Angular3Pt(g) => {
                            g.first_point = Vector3::UNIT_X;
                            g.second_point = Vector3::UNIT_Y;
                            g.angle_vertex = Vector3::ZERO;
                            g.definition_point = Vector3::new(1., 1., 0.);
                        }
                        Dimension::Arc(g) => {
                            g.arc_end_parameter = g.arc_start_parameter + std::f64::consts::TAU
                        }
                        _ => {}
                    }
                }
                use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
                d.base_mut()
                    .common
                    .extended_data
                    .add_record(ExtendedDataRecord {
                        application_name: "Geometry".into(),
                        values: vec![
                            X::Position3D(Vector3::new(1., 2., 3.)),
                            X::Direction3D(Vector3::UNIT_Y),
                            X::Distance(2.5),
                        ],
                    });
                doc.add_entity(EntityType::Dimension(Box::new(d)))?;
            }
        }
    }
    let controls: Vec<_> = doc.entities().take(9).cloned().collect();
    for mut e in controls {
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        doc.add_paper_space_entity(e)?;
    }
    // Canonicalize omitted DXF fields before constructing the shared input.
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn values(d: &Dimension) -> Vec<f64> {
    let b = d.base();
    let bounds = d.bounding_box();
    let mut v = vec![];
    fn p(v: &mut Vec<f64>, p: Vector3) {
        v.extend([p.x, p.y, p.z]);
    }
    p(&mut v, bounds.min);
    p(&mut v, bounds.max);
    v.extend([
        d.measurement(),
        d.stored_measurement(),
        b.actual_measurement,
        b.insertion_rotation,
    ]);
    for q in [
        if let Dimension::Radius(g) = d {
            g.angle_vertex
        } else {
            d.definition_point()
        },
        b.text_middle_point,
        b.insertion_point,
        b.normal,
    ] {
        p(&mut v, q);
    }
    v.extend([b.text_rotation, b.horizontal_direction]);
    match d {
        Dimension::Linear(g) => {
            p(&mut v, g.first_point);
            p(&mut v, g.second_point);
            v.extend([g.ext_line_rotation, g.rotation]);
        }
        Dimension::Aligned(g) => {
            p(&mut v, g.first_point);
            p(&mut v, g.second_point);
            v.push(g.ext_line_rotation);
        }
        Dimension::Radius(g) => {
            p(&mut v, g.definition_point);
            v.push(g.leader_length);
        }
        Dimension::Diameter(g) => {
            p(&mut v, g.angle_vertex);
            v.push(g.leader_length);
        }
        Dimension::Angular2Ln(g) => {
            for q in [
                g.first_point,
                g.second_point,
                g.angle_vertex,
                g.dimension_arc,
            ] {
                p(&mut v, q);
            }
        }
        Dimension::Angular3Pt(g) => {
            for q in [g.first_point, g.second_point, g.angle_vertex] {
                p(&mut v, q);
            }
        }
        Dimension::Ordinate(g) => {
            p(&mut v, g.feature_location);
            p(&mut v, g.leader_endpoint);
        }
        Dimension::Arc(g) => {
            for q in [
                g.first_extension_point,
                g.second_extension_point,
                g.center_point,
                g.first_leader_point,
                g.second_leader_point,
            ] {
                p(&mut v, q);
            }
            v.extend([g.arc_start_parameter, g.arc_end_parameter]);
        }
        Dimension::LargeRadial(g) => {
            for q in [g.chord_point, g.override_center, g.jog_point] {
                p(&mut v, q);
            }
            v.push(g.jog_angle);
        }
    }
    v
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(point($a.$field,$b.$field);$a.$field=$b.$field;)*}}
macro_rules! numbers{($a:ident,$b:ident,$($field:ident),*)=>{$(near($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    let (EntityType::Dimension(a), EntityType::Dimension(b)) = (a, b) else {
        panic!()
    };
    {
        let a = a.base_mut();
        let b = b.base();
        vectors!(
            a,
            b,
            definition_point,
            text_middle_point,
            insertion_point,
            normal
        );
        numbers!(
            a,
            b,
            actual_measurement,
            text_rotation,
            horizontal_direction,
            insertion_rotation,
            line_spacing_factor
        );
    }
    match (a.as_mut(), b.as_ref()) {
        (Dimension::Linear(a), Dimension::Linear(b)) => {
            vectors!(a, b, definition_point, first_point, second_point);
            numbers!(a, b, rotation, ext_line_rotation);
        }
        (Dimension::Aligned(a), Dimension::Aligned(b)) => {
            vectors!(a, b, definition_point, first_point, second_point);
            numbers!(a, b, ext_line_rotation);
        }
        (Dimension::Radius(a), Dimension::Radius(b)) => {
            vectors!(a, b, definition_point, angle_vertex);
            numbers!(a, b, leader_length);
        }
        (Dimension::Diameter(a), Dimension::Diameter(b)) => {
            vectors!(a, b, definition_point, angle_vertex);
            numbers!(a, b, leader_length);
        }
        (Dimension::Angular2Ln(a), Dimension::Angular2Ln(b)) => {
            vectors!(
                a,
                b,
                definition_point,
                first_point,
                second_point,
                angle_vertex,
                dimension_arc
            );
        }
        (Dimension::Angular3Pt(a), Dimension::Angular3Pt(b)) => {
            vectors!(
                a,
                b,
                definition_point,
                first_point,
                second_point,
                angle_vertex
            );
        }
        (Dimension::Ordinate(a), Dimension::Ordinate(b)) => {
            vectors!(a, b, definition_point, feature_location, leader_endpoint);
        }
        (Dimension::Arc(a), Dimension::Arc(b)) => {
            vectors!(
                a,
                b,
                definition_point,
                first_extension_point,
                second_extension_point,
                center_point,
                first_leader_point,
                second_leader_point
            );
            numbers!(a, b, arc_start_parameter, arc_end_parameter);
        }
        (Dimension::LargeRadial(a), Dimension::LargeRadial(b)) => {
            vectors!(
                a,
                b,
                definition_point,
                chord_point,
                override_center,
                jog_point
            );
            numbers!(a, b, jog_angle);
        }
        _ => panic!("Dimension kind changed"),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 81);
    for step in 0..14 {
        eprintln!("dimension step {step}");
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
        assert_eq!(rows.lines().count(), 81);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let handle = p.next().unwrap();
            let h = Handle::new(u64::from_str_radix(handle, 16).unwrap());
            let EntityType::Dimension(d) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let a: Vec<f64> = p.map(|p| p.parse().unwrap()).collect();
            let b = values(d);
            assert_eq!(a.len(), b.len());
            for (i, (a, b)) in a.iter().zip(&b).enumerate() {
                assert!(
                    (a - b).abs() <= 2e-10 * b.abs().max(1.),
                    "step {step}, handle {handle}, value {i}: {a} != {b}"
                );
            }
        }
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), 81);
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.dim_styles, actual.dim_styles);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
