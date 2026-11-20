// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::mline::{MLine, MLineFlags, MLineJustification, MLineSegment, MLineVertex};
use acadrust::objects::{MLineStyle, MLineStyleElement, MLineStyleFlags, ObjectType};
use acadrust::types::{Color, Handle};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn style() -> MLineStyle {
    let mut s = MLineStyle::new("中文多线");
    s.description = "道路断面".into();
    s.flags = MLineStyleFlags::from_bits(1 | 2 | 16 | 512);
    s.start_angle = 0.75;
    s.end_angle = 1.25;
    s.fill_color = Color::from_true_color_value(0x123456);
    s.elements = vec![
        MLineStyleElement::full(1.5, Color::from_index(2), "Continuous"),
        MLineStyleElement::full(-0.5, Color::from_true_color_value(0xab12ef), "BYLAYER"),
    ];
    s
}
pub(crate) fn line(h: Handle, paper: bool) -> MLine {
    let mut m = MLine::new();
    m.style_name = "中文多线".into();
    m.style_handle = Some(h);
    m.flags = MLineFlags::from_bits_truncate(if paper { 9 } else { 7 });
    m.justification = MLineJustification::Bottom;
    m.scale_factor = 2.5;
    m.start_point = Vector3::new(1., 2., 3.);
    m.normal = Vector3::new(0., 0., 2.);
    for i in 0..3 {
        let n = i as f64;
        m.vertices.push(MLineVertex {
            position: Vector3::new(1. + n * 10., 2. + n * 20., 3. + n),
            direction: Vector3::new(1., n, 0.),
            miter: Vector3::new(-n, 1., 0.),
            segments: (0..2)
                .map(|j| MLineSegment {
                    parameters: vec![n + j as f64, 0., 10. + n],
                    area_fill_parameters: vec![1. + n, 4. + n],
                })
                .collect(),
        });
    }
    m
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut s = style();
    s.handle = d.allocate_handle();
    s.owner = d.header.acad_mlinestyle_dict_handle;
    let h = s.handle;
    if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&s.owner) {
        dict.add_entry(&s.name, h);
    } else {
        panic!();
    }
    d.objects.insert(h, ObjectType::MLineStyle(s));
    d.add_entity(EntityType::MLine(line(h, false)))?;
    d.add_paper_space_entity(EntityType::MLine(line(h, true)))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn get_style(d: &CadDocument) -> &MLineStyle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::MLineStyle(s) = o {
                if s.name == "中文多线" {
                    Some(s)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .unwrap()
}
fn get_line(e: &EntityType) -> &MLine {
    if let EntityType::MLine(m) = e {
        m
    } else {
        panic!()
    }
}
fn same(a: &MLine, b: &MLine) {
    let mut b = b.clone();
    b.common = a.common.clone();
    b.style_handle = a.style_handle;
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    let mut expected = get_style(&original).clone();
    let actual = get_style(&created);
    expected.handle = actual.handle;
    expected.owner = actual.owner;
    assert_eq!(actual, &expected);
    for space in ["*Model_Space", "*Paper_Space"] {
        let a = get_line(created.entities_in_block(space).next().unwrap());
        let b = get_line(original.entities_in_block(space).next().unwrap());
        same(a, b);
        assert_eq!(a.style_handle, Some(actual.handle));
    }
    let before = read("before")?;
    let source = before
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let edited = read("edited")?;
    for e in before.entities() {
        let mut expected = get_line(e).clone();
        if e.common().handle == source {
            expected.vertices[1].position = Vector3::new(14., 25., 36.);
            expected.vertices[1].segments[0].parameters = vec![8., 9., 10., 11.];
            expected.vertices[2].segments[1].area_fill_parameters = vec![2., 3.];
            expected.scale_factor = 3.;
        }
        assert_eq!(
            get_line(edited.get_entity(e.common().handle).unwrap()),
            &expected
        );
    }
    let s = get_style(&edited);
    assert_eq!(s.start_angle, 0.5);
    assert_eq!(s.fill_color, Color::ByBlock);
    assert_eq!(s.elements[1].color, Color::from_index(3));
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 3);
    let copy = copied
        .entities()
        .find(|e| before.get_entity(e.common().handle).is_none())
        .unwrap();
    same(get_line(copy), get_line(edited.get_entity(source).unwrap()));
    assert_eq!(
        copied
            .xrecord(copy.common().handle, "Data")
            .unwrap()
            .get_first_by_code(340)
            .unwrap()
            .value
            .as_handle(),
        Some(copy.common().handle)
    );
    let changed = read("copy-edited")?;
    assert_eq!(changed.get_entity(source), edited.get_entity(source));
    assert_eq!(
        get_line(changed.get_entity(copy.common().handle).unwrap()).vertices[0]
            .position
            .x,
        99.
    );
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 1);
    for e in deleted.entities() {
        assert_eq!(Some(e), before.get_entity(e.common().handle));
    }
    let special = read("partial")?;
    let m = get_line(special.entities_in_block("*Model_Space").next().unwrap());
    assert_eq!(m.vertices.len(), 3);
    assert_eq!(m.vertices[0].position, Vector3::ZERO);
    assert_eq!(m.vertices[0].segments[0].area_fill_parameters, vec![1., 4.]);
    println!("Rust verified MLINE/MLINESTYLE fields, radians, RGB, variable parameters, copy independence and object references");
    Ok(())
}
