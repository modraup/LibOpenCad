// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{
    BoundaryEdge, BoundaryPath, BoundaryPathFlags, HatchPatternLine, Line, PolylineEdge,
};
use acadrust::objects::*;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
fn mtext() -> MTextContext {
    MTextContext {
        attachment: 2,
        x_axis_dir: Vector3::UNIT_X,
        insertion: Vector3::new(1., 2., 3.),
        rect_width: 20.,
        rect_height: 10.,
        extents_width: 19.,
        extents_height: 9.,
        column_type: 2,
        columns: Some(MTextColumns {
            num_heights: 2,
            width: 8.,
            gutter: 1.,
            auto_height: false,
            flow_reversed: true,
            heights: vec![4., 5.],
        }),
    }
}
fn hatch() -> HatchScaleContext {
    let mut path = BoundaryPath::with_flags(BoundaryPathFlags::from_bits(3));
    path.edges.push(BoundaryEdge::Polyline(PolylineEdge {
        vertices: vec![
            Vector3::ZERO,
            Vector3::new(10., 0., 0.5),
            Vector3::new(10., 10., 0.),
        ],
        is_closed: true,
    }));
    HatchScaleContext {
        pattern_lines: vec![HatchPatternLine {
            angle: 0.5,
            base_point: Vector2::new(1., 2.),
            offset: Vector2::new(3., 4.),
            dash_lengths: vec![2., -1.],
        }],
        pattern_scale: 2.,
        pattern_base: Vector3::new(1., 2., 3.),
        loops: vec![
            HatchLoopContext {
                loop_type: 3,
                supports_context: false,
                boundary: Some(path),
            },
            HatchLoopContext {
                loop_type: 1,
                supports_context: true,
                boundary: None,
            },
        ],
    }
}
fn kinds(scale: Handle, block: Handle) -> Vec<ObjectContextKind> {
    let mut v = vec![
        ObjectContextKind::AnnotScale,
        ObjectContextKind::BlkRef {
            rotation: 0.5,
            insertion: Vector3::new(1., 2., 3.),
            scale_factor: Vector3::new(2., 3., 4.),
        },
        ObjectContextKind::Text {
            horizontal_mode: 2,
            rotation: 0.25,
            insertion: Vector2::new(1., 2.),
            alignment: Vector2::new(3., 4.),
        },
        ObjectContextKind::MText(mtext()),
        ObjectContextKind::MLeader(Default::default()),
        ObjectContextKind::MTextAttribute(MTextAttributeContext {
            horizontal_mode: 1,
            rotation: 0.5,
            insertion: Vector2::new(1., 2.),
            alignment: Vector2::new(3., 4.),
            enable_context: true,
            context: Some(EmbeddedMTextContext {
                owner_handle: Handle::NULL,
                reactors: vec![],
                xdictionary_handle: None,
                has_binary_data: false,
                class_version: 4,
                has_file_to_extension_dictionary: true,
                is_default: true,
                scale,
                mtext: mtext(),
            }),
        }),
        ObjectContextKind::Leader(LeaderContext {
            points: vec![Vector3::ZERO, Vector3::new(1., 2., 3.)],
            x_direction: Vector3::UNIT_X,
            annotation_enabled: true,
            insertion_offset: Vector3::new(2., 3., 4.),
            endpoint_projection: Vector3::new(5., 6., 7.),
        }),
        ObjectContextKind::Fcf {
            location: Vector3::new(3., 4., 5.),
            horizontal_direction: Vector3::UNIT_X,
        },
        ObjectContextKind::HatchScale(hatch()),
        ObjectContextKind::HatchView(HatchViewContext {
            hatch: hatch(),
            view: Handle::NULL,
            view_normal: Vector3::UNIT_Z,
            view_rotation: 0.25,
            evaluate_hatch: true,
        }),
    ];
    let p = Vector3::new(1., 2., 3.);
    let q = Vector3::new(4., 5., 6.);
    for subtype in [
        DimSubtype::Aligned { dimline_pt: p },
        DimSubtype::Angular { arc_pt: p },
        DimSubtype::Diametric {
            first_arc_pt: p,
            def_pt: q,
        },
        DimSubtype::Radial { first_arc_pt: p },
        DimSubtype::RadialLarge {
            ovr_center: p,
            jog_point: q,
        },
        DimSubtype::Ordinate {
            feature_location_pt: p,
            leader_endpt: q,
        },
    ] {
        v.push(ObjectContextKind::Dim(DimContext {
            def_pt: Vector2::new(7., 8.),
            is_def_textloc: true,
            text_rotation: 0.5,
            block,
            b293: true,
            dimtofl: true,
            dimosxd: false,
            dimatfit: true,
            dimtix: false,
            dimtmove: true,
            override_code: 3,
            has_arrow2: true,
            flip_arrow2: true,
            flip_arrow1: false,
            subtype,
        }));
    }
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let root = d.header.named_objects_dict_handle;
    let scale = d.allocate_handle();
    let mut s = Scale::new("1:50", 1., 50.);
    s.handle = scale;
    s.owner_handle = root;
    d.objects.insert(scale, ObjectType::Scale(s));
    if let ObjectType::Dictionary(v) = d.objects.get_mut(&root).unwrap() {
        v.add_entry("比例", scale);
    }
    for paper in [false, true] {
        let e = EntityType::Line(Line::from_points(Vector3::ZERO, Vector3::UNIT_X));
        let h = if paper {
            d.add_paper_space_entity(e)?
        } else {
            d.add_entity(e)?
        };
        let block = d.get_entity(h).unwrap().common().owner_handle;
        let owner = d.ensure_extension_dictionary(h);
        for (i, kind) in kinds(scale, block).into_iter().enumerate() {
            let id = d.allocate_handle();
            let c = ObjectContextData {
                handle: id,
                owner_handle: owner,
                reactors: vec![owner],
                xdictionary_handle: None,
                class_version: 4,
                has_file_to_extension_dictionary: true,
                uses_legacy_mleader_context_base: false,
                is_default: true,
                scale,
                kind,
            };
            d.register_object_context_class(c.class_name());
            if let ObjectType::Dictionary(v) = d.objects.get_mut(&owner).unwrap() {
                v.add_entry(format!("*A{i}"), id);
            }
            d.objects.insert(id, ObjectType::ObjectContextData(c));
        }
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn contexts(d: &CadDocument) -> Vec<&ObjectContextData> {
    d.objects
        .values()
        .filter_map(|o| {
            if let ObjectType::ObjectContextData(c) = o {
                Some(c)
            } else {
                None
            }
        })
        .collect()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    assert_eq!(contexts(&round).len(), 32);
    for c in contexts(&src) {
        assert_eq!(
            round.objects.get(&c.handle),
            src.objects.get(&c.handle),
            "{}",
            c.class_name()
        );
    }
    let edit = read("edited")?;
    let model = src.entities_in_block("*Model_Space").next().unwrap();
    let owner = src
        .extension_dictionary_handle(model.common().handle)
        .unwrap();
    for c in contexts(&src) {
        if c.owner_handle == owner {
            if let ObjectType::ObjectContextData(v) = &edit.objects[&c.handle] {
                assert_eq!(v.class_version, 9);
                assert!(!v.is_default);
                assert_eq!(v.scale, c.scale);
                match &v.kind {
                    ObjectContextKind::BlkRef { insertion, .. } => assert_eq!(insertion.x, 99.),
                    ObjectContextKind::Text { rotation, .. } => assert_eq!(*rotation, 0.75),
                    ObjectContextKind::MText(t) => {
                        assert_eq!(t.columns.as_ref().unwrap().heights[0], 12.)
                    }
                    ObjectContextKind::MTextAttribute(t) => {
                        assert_eq!(t.context.as_ref().unwrap().mtext.insertion.z, 99.)
                    }
                    ObjectContextKind::Leader(t) => assert_eq!(t.points[1].z, 99.),
                    ObjectContextKind::Fcf { location, .. } => assert_eq!(location.z, 99.),
                    ObjectContextKind::HatchScale(t) => {
                        assert_eq!(t.pattern_lines[0].dash_lengths[0], 9.)
                    }
                    ObjectContextKind::HatchView(t) => assert_eq!(t.view_rotation, 0.75),
                    ObjectContextKind::Dim(t) => {
                        let p = match &t.subtype {
                            DimSubtype::Aligned { dimline_pt } => dimline_pt,
                            DimSubtype::Angular { arc_pt } => arc_pt,
                            DimSubtype::Diametric { first_arc_pt, .. } => first_arc_pt,
                            DimSubtype::Radial { first_arc_pt } => first_arc_pt,
                            DimSubtype::RadialLarge { ovr_center, .. } => ovr_center,
                            DimSubtype::Ordinate {
                                feature_location_pt,
                                ..
                            } => feature_location_pt,
                        };
                        assert_eq!(p.z, 99.);
                    }
                    _ => assert_eq!(v.kind, c.kind),
                }
            } else {
                panic!()
            }
        } else {
            assert_eq!(src.objects.get(&c.handle), edit.objects.get(&c.handle));
        }
    }
    let copied = read("copied")?;
    assert_eq!(contexts(&copied).len(), 48);
    for c in contexts(&copied)
        .into_iter()
        .filter(|c| !src.objects.contains_key(&c.handle))
    {
        assert_eq!(c.class_version, 9);
        assert!(c.reactors.contains(&c.owner_handle));
        assert!(contexts(&edit).iter().any(|e| e.kind == c.kind));
    }
    let deleted = read("deleted")?;
    assert_eq!(contexts(&deleted).len(), 32);
    for c in contexts(&edit) {
        assert_eq!(deleted.objects.get(&c.handle), edit.objects.get(&c.handle));
    }
    let fresh = read("fresh")?;
    assert_eq!(contexts(&fresh).len(), 16);
    for c in contexts(&fresh) {
        assert_eq!(c.class_version, 9);
        assert!(matches!(
            fresh.objects.get(&c.scale),
            Some(ObjectType::Scale(_))
        ));
    }
    println!("Rust verified 16 annotation context kinds, nested MTEXT, hatch loops, copy/delete and neighbor graph");
    Ok(())
}
