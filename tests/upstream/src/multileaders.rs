// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::multileader::*;
use acadrust::tables::BlockRecord;
use acadrust::types::{Color, Handle, LineWeight};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
pub(crate) fn definition(block: Handle, style: Handle, text_style: Handle, with_block: bool) -> MultiLeader {
    let mut v = MultiLeader::new();
    v.style_handle = Some(style);
    v.text_style_handle = Some(text_style);
    v.path_type = MultiLeaderPathType::Spline;
    v.line_color = Color::from_true_color_value(0x123456);
    v.text_color = Color::from_index(4);
    v.enable_landing = false;
    v.dogleg_length = 2.;
    v.arrowhead_size = 1.5;
    v.text_frame = true;
    v.block_scale = Vector3::new(2., 3., 4.);
    v.block_rotation = 0.7;
    v.scale_factor = 2.;
    v.extend_leader_to_text = true;
    if with_block {
        v.set_block_content(block, Vector3::new(7., 8., 9.));
    } else {
        v.set_text_content("中文\\P多重引线", Vector3::new(1., 2., 3.));
    }
    let c = &mut v.context;
    c.text_style_handle = Some(text_style);
    c.scale_factor = 3.;
    c.text_height = 4.;
    c.text_width = 30.;
    c.text_boundary_height = 20.;
    c.text_rotation = 0.25;
    c.text_color = Color::from_true_color_value(0xabcdef);
    c.background_fill_color = Color::ByLayer;
    c.background_fill_enabled = true;
    c.background_transparency = 25;
    c.column_type = 2;
    c.column_width = 10.;
    c.column_gutter = 2.;
    c.column_sizes = vec![10., 20.];
    c.column_flow_reversed = true;
    c.block_content_scale = Vector3::new(2., 3., 4.);
    c.block_rotation = 0.5;
    c.block_content_color = Color::from_index(2);
    c.transform_matrix[12] = 7.;
    c.transform_matrix[13] = 8.;
    c.transform_matrix[14] = 9.;
    for i in 0..2 {
        let mut r = LeaderRoot::new(i);
        r.connection_point = Vector3::new(10. + i as f64, 20., 30.);
        r.direction = Vector3::new(0., 1., 0.);
        r.landing_distance = 3.;
        r.break_points = vec![StartEndPointPair::new(
            Vector3::new(1., 2., 3.),
            Vector3::new(4., 5., 6.),
        )];
        for j in 0..2 {
            let mut l = LeaderLine::from_points(
                j,
                vec![
                    Vector3::new(i as f64, j as f64, 1.),
                    Vector3::new(5., 6., 7.),
                    Vector3::new(8., 9., 10.),
                ],
            );
            l.line_color = Color::from_index(3);
            l.line_weight = LineWeight::from_value(35);
            l.arrowhead_size = 2.;
            l.break_infos = vec![
                LeaderLineBreakInfo {
                    segment_index: 0,
                    break_points: vec![StartEndPointPair::new(
                        Vector3::new(2., 3., 4.),
                        Vector3::new(3., 4., 5.),
                    )],
                },
                LeaderLineBreakInfo {
                    segment_index: 1,
                    break_points: vec![StartEndPointPair::new(
                        Vector3::new(6., 7., 8.),
                        Vector3::new(7., 8., 9.),
                    )],
                },
            ];
            r.lines.push(l);
        }
        v.context.leader_roots.push(r);
    }
    v.block_attributes = vec![BlockAttribute {
        attribute_definition_handle: None,
        index: 7,
        width: 2.5,
        text: "中文属性".into(),
    }];
    v.arrowhead_overrides = vec![
        MultiLeaderArrowheadOverride {
            index: 0,
            is_default: true,
            arrowhead_handle: None,
        },
        MultiLeaderArrowheadOverride {
            index: 1,
            is_default: false,
            arrowhead_handle: Some(block),
        },
    ];
    v
}
fn custom_style() -> acadrust::objects::MultiLeaderStyle {
    let mut s = acadrust::objects::MultiLeaderStyle::new("中文引线样式");
    s.description = "样式说明".into();
    s.default_text = "默认引线".into();
    s.text_height = 5.;
    s.first_segment_angle = 0.5;
    s.second_segment_angle = 1.25;
    s.line_color = Color::from_true_color_value(0x33aa77);
    s.text_color = Color::from_index(6);
    s.block_content_color = Color::None;
    s.enable_block_scale = true;
    s.block_content_scale_x = 2.;
    s.block_content_scale_y = 3.;
    s.block_content_scale_z = 4.;
    s.enable_block_rotation = true;
    s.block_content_rotation = 0.75;
    s.align_space = 1.5;
    s.break_gap_size = 0.5;
    s.max_leader_points = 5;
    s.unknown_flag_298 = true;
    s.text_always_left = true;
    s
}
fn get_style(d: &CadDocument) -> &acadrust::objects::MultiLeaderStyle {
    d.objects
        .values()
        .find_map(|o| {
            if let acadrust::objects::ObjectType::MultiLeaderStyle(s) = o {
                if s.name == "中文引线样式" {
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
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut b = BlockRecord::new("标签块");
    b.handle = d.allocate_handle();
    b.block_entity_handle = d.allocate_handle();
    b.block_end_handle = d.allocate_handle();
    let block = b.handle;
    d.block_records.add(b)?;
    let mut s = custom_style();
    s.handle = d.allocate_handle();
    s.owner_handle = d
        .objects
        .values()
        .find_map(|o| {
            if let acadrust::objects::ObjectType::MultiLeaderStyle(s) = o {
                Some(s.owner_handle)
            } else {
                None
            }
        })
        .unwrap();
    let style = s.handle;
    if let Some(acadrust::objects::ObjectType::Dictionary(dict)) =
        d.objects.get_mut(&s.owner_handle)
    {
        dict.add_entry(&s.name, style);
    }
    d.objects
        .insert(style, acadrust::objects::ObjectType::MultiLeaderStyle(s));
    let text = d.text_styles.get("Standard").unwrap().handle;
    for paper in [false, true] {
        for with_block in [false, true] {
            let e = EntityType::MultiLeader(Box::new(definition(block, style, text, with_block)));
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
fn ml(e: &EntityType) -> &MultiLeader {
    if let EntityType::MultiLeader(v) = e {
        v
    } else {
        panic!()
    }
}
fn same(a: &MultiLeader, b: &MultiLeader) {
    let mut b = b.clone();
    b.common = a.common.clone();
    b.style_handle = a.style_handle;
    b.text_style_handle = a.text_style_handle;
    b.line_type_handle = a.line_type_handle;
    b.block_content_handle = a.block_content_handle;
    b.context.text_style_handle = a.context.text_style_handle;
    b.context.block_content_handle = a.context.block_content_handle;
    for (a, b) in a.arrowhead_overrides.iter().zip(&mut b.arrowhead_overrides) {
        b.arrowhead_handle = a.arrowhead_handle;
    }
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    let a = get_style(&created);
    let mut expected = get_style(&original).clone();
    expected.handle = a.handle;
    expected.owner_handle = a.owner_handle;
    expected.line_type_handle = a.line_type_handle;
    assert_eq!(a, &expected);
    for space in ["*Model_Space", "*Paper_Space"] {
        let expected: Vec<_> = original.entities_in_block(space).map(ml).collect();
        let actual: Vec<_> = created.entities_in_block(space).map(ml).collect();
        assert_eq!(actual.len(), 2);
        for a in actual {
            same(
                a,
                expected
                    .iter()
                    .find(|b| a.content_type == b.content_type)
                    .unwrap(),
            );
        }
    }
    let before = read("before")?;
    let edited = read("edited")?;
    let a = get_style(&edited);
    let mut expected = get_style(&before).clone();
    expected.default_text = "修改默认".into();
    expected.line_color = Color::ByLayer;
    expected.text_height = 7.;
    assert_eq!(a, &expected);
    let sources: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    for e in before.entities() {
        let mut expected = ml(e).clone();
        if sources.contains(&e.common().handle) {
            expected.context.leader_roots[0].lines[0].points[1] = Vector3::new(50., 60., 70.);
            expected.context.leader_roots[1].lines[1].break_infos[1].break_points[0].end_point =
                Vector3::new(17., 18., 19.);
            if expected.context.has_text_contents {
                expected.context.text_string = "修改\\P引线".into();
            }
            expected.arrowhead_size = 3.;
        }
        assert_eq!(ml(edited.get_entity(e.common().handle).unwrap()), &expected);
    }
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 6);
    for e in copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
    {
        let v = ml(e);
        let expected = edited
            .entities_in_block("*Model_Space")
            .map(ml)
            .find(|b| b.content_type == v.content_type)
            .unwrap();
        same(v, expected);
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
    assert_eq!(deleted.entities().count(), 2);
    for e in deleted.entities() {
        assert_eq!(Some(e), before.get_entity(e.common().handle));
    }
    println!("Rust verified MULTILEADER text/block contexts, nested lines and breaks, color methods, reference fields, copy and deletion");
    Ok(())
}
