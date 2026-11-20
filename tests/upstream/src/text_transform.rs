// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::attribute_definition::{
    HorizontalAlignment as AH, VerticalAlignment as AV,
};
use acadrust::entities::{
    AttachmentPoint, AttributeDefinition, AttributeEntity, Insert, MText, MirrorOptions, Text,
    TextHorizontalAlignment as H, TextVerticalAlignment as V,
};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
fn attribute(n: Vector3, h: usize) -> AttributeDefinition {
    let mut a = AttributeDefinition::new("标签".into(), "请输入".into(), "中文abc".into());
    a.normal = n;
    a.insertion_point = Vector3::new(1., 2., 3.);
    a.alignment_point = Vector3::new(4., 5., 6.);
    a.height = 2.5;
    a.rotation = 0.3;
    a.oblique_angle = 0.2;
    a.width_factor = 1.25;
    a.text_generation_flags = 6;
    a.horizontal_alignment = [
        AH::Left,
        AH::Center,
        AH::Right,
        AH::Aligned,
        AH::Middle,
        AH::Fit,
    ][h];
    a.vertical_alignment = if h == 0 { AV::Baseline } else { AV::Top };
    a.lock_position = true;
    if h % 2 == 1 {
        a.is_multiline = true;
        let mut m = MText::with_value("多行\\P中文", Vector3::new(7., 8., 9.));
        m.normal = n;
        m.height = 3.;
        m.rotation = 0.4;
        m.rectangle_height = Some(6.);
        a.embedded_mtext = Some(Box::new(m));
    }
    a
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut block = acadrust::tables::BlockRecord::new("中文块");
    block.handle = d.allocate_handle();
    block.block_entity_handle = d.allocate_handle();
    block.block_end_handle = d.allocate_handle();
    block.base_point = Vector3::new(1., 2., 3.);
    d.block_records.add(block)?;
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        for h in 0..6 {
            for v in 0..4 {
                let mut t = Text::new();
                t.value = "中文abc😀".into();
                t.insertion_point = Vector3::new(1., 2., 3.);
                t.alignment_point = Some(Vector3::new(5., 7., 9.));
                t.height = 2.5;
                t.width_factor = 1.25;
                t.rotation = 0.3;
                t.oblique_angle = -0.15;
                t.thickness = 2.;
                t.normal = n;
                t.generation_flags = if v % 2 == 0 { 0 } else { 6 };
                t.horizontal_alignment =
                    [H::Left, H::Center, H::Right, H::Aligned, H::Middle, H::Fit][h];
                t.vertical_alignment = [V::Baseline, V::Bottom, V::Middle, V::Top][v];
                d.add_entity(EntityType::Text(t))?;
            }
        }
        for a in [
            AttachmentPoint::TopLeft,
            AttachmentPoint::TopCenter,
            AttachmentPoint::TopRight,
            AttachmentPoint::MiddleLeft,
            AttachmentPoint::MiddleCenter,
            AttachmentPoint::MiddleRight,
            AttachmentPoint::BottomLeft,
            AttachmentPoint::BottomCenter,
            AttachmentPoint::BottomRight,
        ] {
            let mut m = MText::with_value("中文\\P多行", Vector3::new(4., 5., 6.));
            m.normal = n;
            m.height = 3.;
            m.rotation = 0.45;
            m.rectangle_width = 20.;
            m.rectangle_height = Some(8.);
            m.attachment_point = a;
            d.add_entity(EntityType::MText(Box::new(m)))?;
        }
        for h in 0..6 {
            d.add_entity(EntityType::AttributeDefinition(attribute(n, h)))?;
        }
        let mut i = Insert::new("中文块", Vector3::new(10., 20., 30.)).with_scale(-2., 3., 4.);
        i.normal = n;
        i.rotation = 0.7;
        i.column_count = 2;
        i.row_count = 3;
        i.column_spacing = -11.;
        i.row_spacing = 13.;
        for h in [0, 1] {
            i.attributes
                .push(AttributeEntity::from_definition(&attribute(n, h), None));
        }
        d.add_entity(EntityType::Insert(i))?;
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
    for name in ["TEXT", "MTEXT", "ATTDEF", "MINSERT"] {
        let mut e = d
            .entities()
            .find(|e| e.as_entity().entity_type() == name)
            .unwrap()
            .clone();
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        if let EntityType::Insert(i) = &mut e {
            for a in &mut i.attributes {
                a.common.handle = Handle::NULL;
            }
            i.seqend_handle = None;
        }
        controls.push(e);
    }
    for e in controls {
        d.add_entity(e)?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn text(a: &mut Text, b: &Text) {
    point(a.insertion_point, b.insertion_point);
    point(a.normal, b.normal);
    assert_eq!(a.alignment_point.is_some(), b.alignment_point.is_some());
    if let (Some(a), Some(b)) = (a.alignment_point, b.alignment_point) {
        point(a, b);
    }
    near(a.height, b.height);
    near(a.rotation, b.rotation);
    near(a.oblique_angle, b.oblique_angle);
    a.oblique_angle = b.oblique_angle;
    a.insertion_point = b.insertion_point;
    a.alignment_point = b.alignment_point;
    a.normal = b.normal;
    a.height = b.height;
    a.rotation = b.rotation;
}
fn mtext(a: &mut MText, b: &MText, embedded: bool) {
    point(a.insertion_point, b.insertion_point);
    point(a.normal, b.normal); // Pinned DXF readers discard an explicit direction with zero XY components.
    if (embedded && b.rotation == 0.) || b.dwg_x_direction.is_some_and(|v| v.x == 0. && v.y == 0.) {
        assert!(a.dwg_x_direction.is_none());
        near(a.rotation, 0.);
        a.dwg_x_direction = b.dwg_x_direction;
    }
    assert_eq!(
        a.dwg_x_direction.is_some(),
        b.dwg_x_direction.is_some(),
        "directions {:?} {:?}; text {}",
        a.dwg_x_direction,
        b.dwg_x_direction,
        b.value
    );
    if let (Some(a), Some(b)) = (a.dwg_x_direction, b.dwg_x_direction) {
        point(a, b);
    }
    near(a.height, b.height);
    near(a.rotation, b.rotation);
    near(a.rectangle_width, b.rectangle_width);
    if b.rectangle_height == Some(0.) {
        assert!(a.rectangle_height.is_none());
        a.rectangle_height = b.rectangle_height;
    }
    assert_eq!(a.rectangle_height.is_some(), b.rectangle_height.is_some());
    if let (Some(a), Some(b)) = (a.rectangle_height, b.rectangle_height) {
        near(a, b);
    }
    a.insertion_point = b.insertion_point;
    a.normal = b.normal;
    a.dwg_x_direction = b.dwg_x_direction;
    a.height = b.height;
    a.rotation = b.rotation;
    a.rectangle_width = b.rectangle_width;
    a.rectangle_height = b.rectangle_height;
}
macro_rules! attribute_geometry {
    ($a:expr,$b:expr) => {{
        let a = $a;
        let b = $b;
        point(a.insertion_point, b.insertion_point);
        point(a.alignment_point, b.alignment_point);
        point(a.normal, b.normal);
        near(a.height, b.height);
        near(a.rotation, b.rotation);
        near(a.oblique_angle, b.oblique_angle);
        a.oblique_angle = b.oblique_angle;
        a.insertion_point = b.insertion_point;
        a.alignment_point = b.alignment_point;
        a.normal = b.normal;
        a.height = b.height;
        a.rotation = b.rotation;
        assert_eq!(a.embedded_mtext.is_some(), b.embedded_mtext.is_some());
        if let (Some(a), Some(b)) = (&mut a.embedded_mtext, &b.embedded_mtext) {
            mtext(a, b, true);
        }
    }};
}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Text(a), EntityType::Text(b)) => text(a, b),
        (EntityType::MText(a), EntityType::MText(b)) => mtext(a, b, false),
        (EntityType::AttributeDefinition(a), EntityType::AttributeDefinition(b)) => {
            attribute_geometry!(a, b)
        }
        (EntityType::AttributeEntity(a), EntityType::AttributeEntity(b)) => {
            attribute_geometry!(a, b)
        }
        (EntityType::Insert(a), EntityType::Insert(b)) => {
            point(a.insert_point, b.insert_point);
            point(a.normal, b.normal);
            near(a.rotation, b.rotation);
            near(a.x_scale(), b.x_scale());
            near(a.y_scale(), b.y_scale());
            near(a.z_scale(), b.z_scale());
            near(a.column_spacing, b.column_spacing);
            near(a.row_spacing, b.row_spacing);
            a.insert_point = b.insert_point;
            a.normal = b.normal;
            a.rotation = b.rotation;
            a.set_x_scale(b.x_scale());
            a.set_y_scale(b.y_scale());
            a.set_z_scale(b.z_scale());
            a.column_spacing = b.column_spacing;
            a.row_spacing = b.row_spacing;
            assert_eq!(a.attributes.len(), b.attributes.len());
            for (a, b) in a.attributes.iter_mut().zip(&b.attributes) {
                attribute_geometry!(a, b);
            }
        }
        _ => panic!("Unexpected text transform type"),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 164);
    for step in 0..18 {
        eprintln!("text transform step {step}");
        let result = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        let t = crate::entity_transform::operation(if step < 14 { step } else { 6 });
        let mut o = MirrorOptions::default();
        if step == 14 {
            o.mirror_text = true;
        }
        if step == 15 {
            o.text_width = Some(19.25);
        }
        if step == 16 {
            o.text_width = Some(f64::NAN);
        }
        if step == 17 {
            o.text_width = Some(-1.);
        }
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" {
                continue;
            }
            if step == 0 {
                e.translate(Vector3::new(3., -4., 5.));
            } else if [5, 6, 7, 11, 14, 15, 16, 17].contains(&step) {
                e.apply_mirror_with_options(&t, o);
            } else {
                e.apply_transform(&t);
            }
        }
        assert_eq!(result.entities().count(), 164);
        for e in expected.entities() {
            let a = result.get_entity(e.common().handle).unwrap();
            crate::entity_transform::compare(a, e);
        }
        let data = std::fs::read_to_string(format!("{prefix}.{step}.insert"))?;
        let mut count = 0;
        for row in data.lines() {
            let words: Vec<_> = row.split_whitespace().collect();
            let h = Handle::new(u64::from_str_radix(words[0], 16).unwrap());
            let EntityType::Insert(i) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let n: usize = words[1].parse().unwrap();
            let points = i.array_points();
            assert_eq!(points.len(), n);
            assert_eq!(words.len(), 8 + n * 3);
            let expected = [
                i.get_transform().apply(Vector3::new(1., 2., 3.)),
                i.get_transform().apply_rotation(Vector3::new(-2., 1., 4.)),
            ]
            .into_iter()
            .chain(points);
            for (word, p) in words[2..].chunks_exact(3).zip(expected) {
                point(
                    Vector3::new(
                        word[0].parse().unwrap(),
                        word[1].parse().unwrap(),
                        word[2].parse().unwrap(),
                    ),
                    p,
                );
            }
            count += 1;
        }
        assert_eq!(count, 5);
        assert_eq!(source.header, result.header);
        assert_eq!(source.block_records, result.block_records);
        assert_eq!(source.objects, result.objects);
    }
    Ok(())
}
