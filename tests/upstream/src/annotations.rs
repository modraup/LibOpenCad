// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::leader::{HooklineDirection, LeaderCreationType, LeaderPathType};
use acadrust::entities::{Leader, Tolerance};
use acadrust::tables::DimStyle;
use acadrust::types::{Color, Handle};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
pub(crate) fn tolerance() -> Tolerance {
    let mut v = Tolerance::new();
    v.text = "{\\Fgdt;p}%%v0.5%%v基准A".into();
    v.dimension_style_name = "中文标注".into();
    v.insertion_point = Vector3::new(10., 20., 30.);
    v.direction = Vector3::new(0., 2., 0.);
    v.normal = Vector3::new(0., 0., 2.);
    v
}
pub(crate) fn leader(h: Handle) -> Leader {
    let mut v = Leader::new();
    v.dimension_style = "中文标注".into();
    v.vertices = vec![
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 5., 6.),
        Vector3::new(7., 8., 9.),
    ];
    v.path_type = LeaderPathType::Spline;
    v.creation_type = LeaderCreationType::WithTolerance;
    v.hookline_direction = HooklineDirection::Same;
    v.hookline_enabled = true;
    v.text_height = 3.;
    v.text_width = 7.;
    v.override_color = Color::from_index(4);
    v.annotation_handle = h;
    v.horizontal_direction = Vector3::new(0., 1., 0.);
    v.block_offset = Vector3::new(2., 3., 4.);
    v.annotation_offset = Vector3::new(5., 6., 7.);
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.dim_styles.add(DimStyle::new("中文标注"))?;
    let h = d.add_entity(EntityType::Tolerance(tolerance()))?;
    d.add_entity(EntityType::Leader(leader(h)))?;
    let h = d.add_paper_space_entity(EntityType::Tolerance(tolerance()))?;
    d.add_paper_space_entity(EntityType::Leader(leader(h)))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn geometry(a: &EntityType, b: &EntityType) {
    let mut b = b.clone();
    *b.common_mut() = a.common().clone();
    if let (EntityType::Leader(a), EntityType::Leader(b)) = (a, &mut b) {
        b.annotation_handle = a.annotation_handle;
    }
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    for space in ["*Model_Space", "*Paper_Space"] {
        let a: Vec<_> = created.entities_in_block(space).collect();
        let b: Vec<_> = original.entities_in_block(space).collect();
        assert_eq!(a.len(), 2);
        for (a, b) in a.into_iter().zip(b) {
            geometry(a, b);
            if let EntityType::Leader(l) = a {
                assert!(matches!(
                    created.get_entity(l.annotation_handle),
                    Some(EntityType::Tolerance(_))
                ));
            }
        }
    }
    let before = read("before")?;
    let model: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    let edited = read("edited")?;
    for e in before.entities() {
        let mut expected = e.clone();
        if e.common().handle == model[0] {
            let EntityType::Tolerance(t) = &mut expected else {
                panic!()
            };
            t.text = "{\\Fgdt;c}%%v0.25".into();
            t.insertion_point = Vector3::new(11., 22., 33.);
        }
        if e.common().handle == model[1] {
            let EntityType::Leader(l) = &mut expected else {
                panic!()
            };
            l.vertices[1] = Vector3::new(44., 55., 66.);
            l.vertices.push(Vector3::new(10., 11., 12.));
            l.arrow_enabled = false;
            l.override_color = Color::ByBlock;
        }
        assert_eq!(edited.get_entity(e.common().handle).unwrap(), &expected);
    }
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 6);
    for e in copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
    {
        let source = if matches!(e, EntityType::Tolerance(_)) {
            model[0]
        } else {
            model[1]
        };
        geometry(e, edited.get_entity(source).unwrap());
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
        if let EntityType::Leader(l) = e {
            assert_eq!(l.annotation_handle, model[0]);
        }
    }
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 2);
    for e in deleted.entities() {
        assert_eq!(before.get_entity(e.common().handle).unwrap(), e);
    }
    let renamed = read("renamed")?;
    for e in renamed.entities() {
        match e {
            EntityType::Leader(l) => assert_eq!(l.dimension_style, "改名标注"),
            EntityType::Tolerance(t) => assert_eq!(t.dimension_style_name, "改名标注"),
            _ => panic!(),
        }
    }
    let partial = read("partial")?;
    let l = partial
        .entities_in_block("*Model_Space")
        .find_map(|e| {
            if let EntityType::Leader(l) = e {
                Some(l)
            } else {
                None
            }
        })
        .unwrap();
    assert_eq!(l.creation_type, LeaderCreationType::NoAnnotation);
    assert_eq!(l.hookline_direction, HooklineDirection::Same);
    assert_eq!(l.vertices.len(), 2);
    assert_eq!(l.origin, Vector3::new(4., 5., 6.));
    println!("Rust verified LEADER/TOLERANCE geometry, style names, annotation handles, copy/deletion and omitted defaults");
    Ok(())
}
