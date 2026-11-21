// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::mtext::MTextColumnData;
use acadrust::entities::{AttachmentPoint, DrawingDirection, LineSpacingStyle, MText};
use acadrust::types::{Color, DxfVersion};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
use std::io::Cursor;
fn definition() -> MText {
    let mut t = MText::new();
    t.value = format!("{}{{\\C1;红色}}\\P第二行\\S1/2;", "中".repeat(90));
    t.insertion_point = Vector3::new(1., 2., 3.);
    t.height = 2.;
    t.rectangle_width = 40.;
    t.rectangle_height = Some(12.);
    t.rotation = 0.5;
    t.attachment_point = AttachmentPoint::MiddleCenter;
    t.drawing_direction = DrawingDirection::TopToBottom;
    t.line_spacing_style = LineSpacingStyle::Exactly;
    t.line_spacing_factor = 1.25;
    t.extents_width = 35.;
    t.extents_height = 8.;
    t.background_fill_flags = 17;
    t.background_scale = 1.75;
    t.background_color = Color::from_rgb(12, 34, 56);
    t.background_transparency = 0x02000080;
    t.column_data = MTextColumnData {
        column_type: 2,
        column_count: 2,
        flow_reversed: true,
        auto_height: false,
        width: 18.,
        gutter: 4.,
        heights: vec![9., 11.],
    };
    t
}
pub fn generate(prefix: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.add_entity(EntityType::MText(Box::new(definition())))?;
    d.add_paper_space_entity(EntityType::MText(Box::new(definition())))?;
    for (name, version) in [("2018", DxfVersion::AC1032), ("2007", DxfVersion::AC1021)] {
        DxfWriter::new(&d)
            .with_version(version)
            .write_to_file(format!("{prefix}-{name}-ascii.dxf"))?;
        DxfWriter::new_binary(&d)
            .with_version(version)
            .write_to_file(format!("{prefix}-{name}-binary.dxf"))?;
    }
    Ok(())
}
fn geometry(a: &MText, b: &MText) {
    let mut b = b.clone();
    b.common = a.common.clone();
    if let (Some(actual), Some(expected)) = (a.dwg_x_direction, b.dwg_x_direction) {
        // C++ and Rust may round the direction computed from the same angle differently.
        for (actual, expected) in [
            (actual.x, expected.x),
            (actual.y, expected.y),
            (actual.z, expected.z),
        ] {
            assert!(
                (actual - expected).abs() <= 4. * f64::EPSILON,
                "MTEXT direction component {actual} differs from {expected}"
            );
        }
        b.dwg_x_direction = Some(actual);
    }
    assert_eq!(a, &b);
}
fn mtext(e: &EntityType) -> &MText {
    if let EntityType::MText(t) = e {
        t
    } else {
        panic!("MTEXT expected")
    }
}
fn edit(t: &mut MText) {
    t.value = "已修改\\P{\\H2x;大字}".into();
    t.insertion_point = Vector3::new(7., 8., 9.);
    t.rotation = -0.75;
    t.dwg_x_direction = None;
    t.column_data.heights = vec![15., 17., 19.];
    t.column_data.column_count = 3;
    t.column_data.width = 12.;
    t.column_data.gutter = 2.;
    t.background_color = Color::from_index(4);
    t.attachment_point = AttachmentPoint::BottomRight;
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let input = DxfReader::from_file(input)?.read()?;
    let read = |name: &str| DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read();
    let made = read("created")?;
    assert_eq!(made.entities().count(), 2);
    for (a, b) in made.entities().zip(input.entities()) {
        geometry(mtext(a), mtext(b));
    }
    let before = read("before")?;
    let edited = read("edited")?;
    let copied = read("copied")?;
    let deleted = read("deleted")?;
    let model = before
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let paper = before
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let mut expected = mtext(before.get_entity(model).unwrap()).clone();
    edit(&mut expected);
    // Materialize writer direction defaults before comparison; remove old ACAD column
    // XDATA because upstream intentionally preserves existing column blocks on output.
    expected.common.extended_data = Default::default();
    let mut reference = CadDocument::new();
    reference.add_entity(EntityType::MText(Box::new(expected)))?;
    let reference =
        DxfReader::from_reader(Cursor::new(DxfWriter::new(&reference).write_to_vec()?))?.read()?;
    geometry(
        mtext(edited.get_entity(model).unwrap()),
        mtext(reference.entities().next().unwrap()),
    );
    assert_eq!(before.get_entity(paper), edited.get_entity(paper));
    let clones: Vec<_> = copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
        .collect();
    assert_eq!(clones.len(), 1);
    let c = clones[0];
    let t = mtext(c);
    assert_eq!(t.value, "副本\\P独立");
    assert_eq!(t.column_data.heights, vec![15., 17., 19.]);
    assert_eq!(
        c.common().owner_handle,
        before.get_entity(paper).unwrap().common().owner_handle
    );
    assert_eq!(
        copied
            .xrecord(c.common().handle, "Data")
            .unwrap()
            .get_first_by_code(340)
            .unwrap()
            .value
            .as_handle(),
        Some(c.common().handle)
    );
    assert_eq!(copied.get_entity(model), edited.get_entity(model));
    assert_eq!(copied.get_entity(paper), before.get_entity(paper));
    assert_eq!(deleted.entities().count(), 1);
    assert_eq!(deleted.get_entity(paper), before.get_entity(paper));
    let mut expected = before.clone();
    expected.remove_entity(model).unwrap();
    assert_eq!(deleted.objects, expected.objects);
    let raw = read("raw")?;
    let texts: Vec<_> = raw.entities().map(mtext).collect();
    assert!((texts[0].rotation - std::f64::consts::FRAC_PI_2).abs() < 1e-12);
    assert_eq!(texts[0].column_data.heights, vec![9., 11.]);
    assert_eq!(texts[1].rotation, 0.);
    assert!(texts[1].dwg_x_direction.is_none());
    println!("Rust verified MTEXT Unicode chunks, geometry, background, columns, copy/delete and rotation precedence");
    Ok(())
}
