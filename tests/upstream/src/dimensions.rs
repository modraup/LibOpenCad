// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::dimension::*;
use acadrust::tables::DimStyle;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
pub(crate) fn definition(kind: usize) -> Dimension {
    let a = Vector3::new(10., 20., 30.);
    let b = Vector3::new(40., 50., 60.);
    let p = Vector3::new(1., 2., 3.);
    let c = Vector3::new(2., 3., 4.);
    let mut d = match kind {
        0 => {
            let mut v = DimensionLinear::rotated(a, b, 0.5);
            v.definition_point = p;
            v.ext_line_rotation = 0.25;
            Dimension::Linear(v)
        }
        1 => {
            let mut v = DimensionAligned::new(a, b);
            v.definition_point = p;
            v.ext_line_rotation = 0.25;
            Dimension::Aligned(v)
        }
        2 => {
            let mut v = DimensionAngular2Ln::default();
            v.definition_point = p;
            v.first_point = a;
            v.second_point = b;
            v.angle_vertex = c;
            v.dimension_arc = Vector3::new(5., 6., 7.);
            Dimension::Angular2Ln(v)
        }
        3 => Dimension::Diameter(DimensionDiameter::new(a, p)),
        4 => {
            let mut v = DimensionRadius::new(p, a);
            v.leader_length = 2.;
            Dimension::Radius(v)
        }
        5 => {
            let mut v = DimensionAngular3Pt::default();
            v.definition_point = p;
            v.first_point = a;
            v.second_point = b;
            v.angle_vertex = c;
            Dimension::Angular3Pt(v)
        }
        6 => {
            let mut v = DimensionOrdinate::new(a, b, false);
            v.definition_point = p;
            Dimension::Ordinate(v)
        }
        7 => {
            let mut v = DimensionArc::default();
            v.definition_point = p;
            v.first_extension_point = a;
            v.second_extension_point = b;
            v.center_point = c;
            v.is_partial = true;
            v.has_leader = true;
            v.arc_start_parameter = 0.2;
            v.arc_end_parameter = 2.4;
            v.first_leader_point = Vector3::new(5., 6., 7.);
            v.second_leader_point = Vector3::new(8., 9., 10.);
            Dimension::Arc(v)
        }
        _ => {
            let mut v = DimensionLargeRadial::default();
            v.definition_point = p;
            v.jog_point = a;
            v.override_center = b;
            v.chord_point = c;
            v.jog_angle = 0.75;
            Dimension::LargeRadial(v)
        }
    };
    let v = d.base_mut();
    v.text_middle_point = Vector3::new(4., 5., 6.);
    v.insertion_point = Vector3::new(7., 8., 9.);
    v.normal = Vector3::new(0., 1., 1.);
    v.style_name = "中文标注".into();
    v.text = "尺寸<>".into();
    v.actual_measurement = 123.25 + kind as f64;
    v.text_rotation = 0.25;
    v.horizontal_direction = 0.5;
    v.line_spacing_factor = 1.25;
    v.line_spacing_style = 2;
    v.attachment_point = AttachmentPointType::BottomCenter;
    v.text_user_positioned = true;
    v.flip_arrow1 = true;
    v.flip_arrow2 = true;
    d
}
fn kind(d: &Dimension) -> usize {
    match d {
        Dimension::Linear(_) => 0,
        Dimension::Aligned(_) => 1,
        Dimension::Angular2Ln(_) => 2,
        Dimension::Diameter(_) => 3,
        Dimension::Radius(_) => 4,
        Dimension::Angular3Pt(_) => 5,
        Dimension::Ordinate(_) => 6,
        Dimension::Arc(_) => 7,
        Dimension::LargeRadial(_) => 8,
    }
}
fn change(d: &mut Dimension) {
    let b = d.base_mut();
    b.text_rotation = -0.75;
    b.line_spacing_factor = 1.5;
    b.text = "修改<>".into();
    match d {
        Dimension::Linear(v) => v.first_point.x += 10.,
        Dimension::Aligned(v) => v.first_point.x += 10.,
        Dimension::Angular2Ln(v) => v.first_point.x += 10.,
        Dimension::Angular3Pt(v) => v.first_point.x += 10.,
        Dimension::Diameter(v) => v.angle_vertex.x += 10.,
        Dimension::Radius(v) => v.definition_point.x += 10.,
        Dimension::Ordinate(v) => {
            v.feature_location.x += 10.;
            v.refresh_measurement();
        }
        Dimension::Arc(v) => v.first_extension_point.x += 10.,
        Dimension::LargeRadial(v) => v.jog_point.x += 10.,
    }
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.dim_styles.add(DimStyle::new("中文标注"))?;
    for paper in [false, true] {
        for i in 0..9 {
            let e = EntityType::Dimension(Box::new(definition(i)));
            if paper {
                d.add_paper_space_entity(e)?;
            } else {
                d.add_entity(e)?;
            }
        }
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn geometry(a: &Dimension, b: &Dimension) {
    let mut b = b.clone();
    b.base_mut().common = a.base().common.clone();
    let x = a.base().actual_measurement;
    let y = b.base().actual_measurement;
    assert!((x - y).abs() < 1e-12);
    b.base_mut().actual_measurement = x;
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    let dim = |e: &EntityType| {
        if let EntityType::Dimension(d) = e {
            Some(d.clone())
        } else {
            None
        }
    };
    for space in ["*Model_Space", "*Paper_Space"] {
        let a: Vec<_> = created.entities_in_block(space).filter_map(dim).collect();
        let b: Vec<_> = original.entities_in_block(space).filter_map(dim).collect();
        assert_eq!(a.len(), 9);
        for a in a {
            geometry(&a, b.iter().find(|b| kind(b) == kind(&a)).unwrap());
        }
    }
    let before = read("before")?;
    let sources: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    let edited = read("edited")?;
    for e in before.entities() {
        let mut expected = dim(e).unwrap();
        if sources.contains(&e.common().handle) {
            change(&mut expected);
        }
        geometry(
            &dim(edited.get_entity(e.common().handle).unwrap()).unwrap(),
            &expected,
        );
    }
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 27);
    for e in copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
    {
        let actual = dim(e).unwrap();
        let expected = edited
            .entities_in_block("*Model_Space")
            .filter_map(dim)
            .find(|d| kind(d) == kind(&actual))
            .unwrap();
        geometry(&actual, &expected);
        assert_eq!(
            copied
                .xrecord(e.common().handle, "Data")
                .unwrap()
                .get_first_by_code(340)
                .unwrap()
                .value
                .as_handle(),
            Some(e.common().handle)
        );
    }
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 9);
    for e in deleted.entities() {
        assert_eq!(before.get_entity(e.common().handle).unwrap(), e);
    }
    let renamed = read("renamed")?;
    for e in renamed.entities() {
        assert_eq!(dim(e).unwrap().base().style_name, "改名标注");
    }
    let cleared = read("cleared")?;
    for e in cleared.entities() {
        let v = dim(e).unwrap();
        assert!(v.base().text_override().is_none());
        assert!(v.base().block_name.is_empty());
    }
    let gaps = read("reader-gaps")?;
    for e in gaps.entities() {
        match dim(e).unwrap().as_ref() {
            Dimension::Diameter(v) => assert_eq!(v.leader_length, 0.),
            Dimension::Arc(v) => {
                assert_eq!(v.base.insertion_point, Vector3::ZERO);
                assert_eq!(v.base.horizontal_direction, 0.);
                assert!(!v.base.flip_arrow1 && !v.base.flip_arrow2);
            }
            Dimension::LargeRadial(v) => {
                assert_eq!(v.base.insertion_point, Vector3::ZERO);
                assert_eq!(v.base.horizontal_direction, 0.);
                assert!(!v.base.flip_arrow1 && !v.base.flip_arrow2);
            }
            _ => {}
        }
    }
    for (stage, count) in [("created", 2), ("copied", 3), ("deleted", 1)] {
        let d = read(stage)?;
        for name in ["ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"] {
            assert_eq!(d.classes.get_by_name(name).unwrap().instance_count, count);
        }
    }
    println!("Rust verified nine DIMENSION kinds, subclass scopes, cached/ordinate measurements, copy/delete, style names and class counts");
    Ok(())
}
