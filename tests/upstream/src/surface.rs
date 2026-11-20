// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::primitives::build_box;
use acadrust::entities::{
    AcisData, EmbeddedEntity, Line, Surface, SurfaceData, SurfaceKind, SurfaceSweepOptions,
};
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType, Vector3};
use std::{fs, path::Path};
fn matrix() -> [f64; 16] {
    [
        1., 0.2, 0.3, 0., 0.4, 2., 0.5, 0., 0.6, 0.7, 3., 0., 4., 5., 6., 1.,
    ]
}
fn options() -> SurfaceSweepOptions {
    SurfaceSweepOptions {
        draft_angle: 0.2,
        draft_start_distance: 1.5,
        draft_end_distance: 2.5,
        twist_angle: 0.3,
        scale_factor: 1.7,
        align_angle: -0.4,
        sweep_entity_transform: matrix(),
        path_entity_transform: matrix().map(|x| x * 2.),
        is_solid: true,
        sweep_alignment_flags: 7,
        path_flags: 9,
        align_start: true,
        bank: true,
        base_point_set: true,
        sweep_entity_transform_computed: true,
        path_entity_transform_computed: true,
        reference_vector: Vector3::new(2., 3., 4.),
    }
}
pub(crate) fn setup(s: &mut Surface) {
    let line = EmbeddedEntity::Line(Line::from_points(
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 6., 8.),
    ));
    let body = EmbeddedEntity::Body {
        type_code: 77,
        acis_data: AcisData::from_sat(&build_box([0., 0., 0.], 2., 3., 4.).to_sat_string()),
    };
    match &mut s.surface_data {
        SurfaceData::Extruded {
            sweep_entity,
            options: o,
            sweep_vector,
            sweep_transform,
        } => {
            *sweep_entity = Some(body);
            *o = options();
            *sweep_vector = Vector3::new(3., 4., 5.);
            *sweep_transform = matrix();
        }
        SurfaceData::Lofted {
            loft_transform,
            cross_section_entities,
            guide_entities,
            path_entity,
            plane_normal_lofting_type,
            start_draft_angle,
            end_draft_angle,
            start_draft_magnitude,
            end_draft_magnitude,
            arc_length_parameterization,
            no_twist,
            align_direction,
            simple_surfaces,
            closed_surfaces,
            solid,
            ruled_surface,
            virtual_guide,
            ..
        } => {
            *loft_transform = matrix();
            *cross_section_entities = vec![line.clone(), body];
            *guide_entities = vec![line.clone()];
            *path_entity = Some(line);
            *plane_normal_lofting_type = 3;
            *start_draft_angle = 0.2;
            *end_draft_angle = 0.3;
            *start_draft_magnitude = 4.;
            *end_draft_magnitude = 5.;
            *arc_length_parameterization = true;
            *no_twist = true;
            *align_direction = true;
            *simple_surfaces = true;
            *closed_surfaces = true;
            *solid = true;
            *ruled_surface = true;
            *virtual_guide = true;
        }
        SurfaceData::Revolved {
            revolve_entity,
            axis_point,
            axis_vector,
            revolve_angle,
            start_angle,
            entity_transform,
            draft_angle,
            draft_start_distance,
            draft_end_distance,
            twist_angle,
            solid,
            close_to_axis,
            ..
        } => {
            *revolve_entity = Some(line);
            *axis_point = Vector3::new(1., 2., 3.);
            *axis_vector = Vector3::new(2., 3., 4.);
            *revolve_angle = 2.;
            *start_angle = 0.2;
            *entity_transform = matrix();
            *draft_angle = 0.3;
            *draft_start_distance = 4.;
            *draft_end_distance = 5.;
            *twist_angle = 0.4;
            *solid = true;
            *close_to_axis = true;
        }
        SurfaceData::Swept {
            sweep_entity,
            path_entity,
            sweep_transform,
            path_transform,
            options: o,
            ..
        } => {
            *sweep_entity = Some(body);
            *path_entity = Some(line);
            *sweep_transform = matrix();
            *path_transform = matrix().map(|x| x * 3.);
            *o = options();
        }
        SurfaceData::Nurb {
            short_170,
            cv_hull_display,
            u_vector1,
            v_vector1,
            u_vector2,
            v_vector2,
        } => {
            *short_170 = 12;
            *cv_hull_display = true;
            *u_vector1 = Vector3::new(1., 2., 3.);
            *v_vector1 = Vector3::new(4., 5., 6.);
            *u_vector2 = Vector3::new(7., 8., 9.);
            *v_vector2 = Vector3::new(10., 11., 12.);
        }
        _ => panic!("kind"),
    }
}
pub(crate) fn change(s: &mut Surface) {
    s.u_isolines = 13;
    s.v_isolines = 17;
    match &mut s.surface_data {
        SurfaceData::Extruded {
            sweep_vector,
            options,
            ..
        } => {
            *sweep_vector = Vector3::new(9., 8., 7.);
            options.scale_factor = 2.3;
            options.bank = false;
        }
        SurfaceData::Lofted {
            start_draft_angle,
            no_twist,
            ..
        } => {
            *start_draft_angle = 0.8;
            *no_twist = false;
        }
        SurfaceData::Revolved {
            revolve_angle,
            axis_point,
            ..
        } => {
            *revolve_angle = 1.2;
            *axis_point = Vector3::new(-1., -2., -3.);
        }
        SurfaceData::Swept { options, .. } => {
            options.twist_angle = 1.4;
            options.align_start = false;
        }
        SurfaceData::Nurb {
            cv_hull_display,
            u_vector2,
            ..
        } => {
            *cv_hull_display = false;
            *u_vector2 = Vector3::new(2., 4., 6.);
        }
        _ => panic!("kind"),
    }
}
fn vec(v: Vector3) -> [f64; 3] {
    [v.x, v.y, v.z]
}
fn option_values(v: &SurfaceSweepOptions) -> Vec<f64> {
    let mut a = vec![
        v.draft_angle,
        v.draft_start_distance,
        v.draft_end_distance,
        v.twist_angle,
        v.scale_factor,
        v.align_angle,
    ];
    a.extend(v.sweep_entity_transform);
    a.extend(v.path_entity_transform);
    a.extend([
        v.is_solid as i32 as f64,
        v.sweep_alignment_flags as f64,
        v.path_flags as f64,
        v.align_start as i32 as f64,
        v.bank as i32 as f64,
        v.base_point_set as i32 as f64,
        v.sweep_entity_transform_computed as i32 as f64,
        v.path_entity_transform_computed as i32 as f64,
    ]);
    a.extend(vec(v.reference_vector));
    a
}
pub(crate) fn values(s: &Surface) -> Vec<f64> {
    let mut a = vec![s.u_isolines as f64, s.v_isolines as f64];
    match &s.surface_data {
        SurfaceData::Extruded {
            sweep_vector,
            sweep_transform,
            options,
            ..
        } => {
            a.extend(vec(*sweep_vector));
            a.extend(sweep_transform);
            a.extend(option_values(options));
        }
        SurfaceData::Lofted {
            loft_transform,
            plane_normal_lofting_type,
            start_draft_angle,
            end_draft_angle,
            start_draft_magnitude,
            end_draft_magnitude,
            arc_length_parameterization,
            no_twist,
            align_direction,
            simple_surfaces,
            closed_surfaces,
            solid,
            ruled_surface,
            virtual_guide,
            ..
        } => {
            a.extend(loft_transform);
            a.extend([
                *plane_normal_lofting_type as f64,
                *start_draft_angle,
                *end_draft_angle,
                *start_draft_magnitude,
                *end_draft_magnitude,
            ]);
            a.extend(
                [
                    *arc_length_parameterization,
                    *no_twist,
                    *align_direction,
                    *simple_surfaces,
                    *closed_surfaces,
                    *solid,
                    *ruled_surface,
                    *virtual_guide,
                ]
                .map(|b| b as i32 as f64),
            );
        }
        SurfaceData::Revolved {
            axis_point,
            axis_vector,
            revolve_angle,
            start_angle,
            entity_transform,
            draft_angle,
            draft_start_distance,
            draft_end_distance,
            twist_angle,
            solid,
            close_to_axis,
            ..
        } => {
            a.extend(vec(*axis_point));
            a.extend(vec(*axis_vector));
            a.extend([*revolve_angle, *start_angle]);
            a.extend(entity_transform);
            a.extend([
                *draft_angle,
                *draft_start_distance,
                *draft_end_distance,
                *twist_angle,
                *solid as i32 as f64,
                *close_to_axis as i32 as f64,
            ]);
        }
        SurfaceData::Swept {
            sweep_transform,
            path_transform,
            options,
            ..
        } => {
            a.extend(sweep_transform);
            a.extend(path_transform);
            a.extend(option_values(options));
        }
        SurfaceData::Nurb {
            short_170,
            cv_hull_display,
            u_vector1,
            v_vector1,
            u_vector2,
            v_vector2,
        } => {
            a.extend([*short_170 as f64, *cv_hull_display as i32 as f64]);
            for v in [u_vector1, v_vector1, u_vector2, v_vector2] {
                a.extend(vec(*v));
            }
        }
        _ => {},
    };
    a
}
fn profiles(s: &Surface) -> String {
    match &s.surface_data{SurfaceData::Extruded{sweep_entity,..}=>format!("{sweep_entity:?}"),SurfaceData::Lofted{cross_section_entities,guide_entities,path_entity,cross_sections,guide_curves,path_curve,..}=>format!("{cross_section_entities:?}{guide_entities:?}{path_entity:?}{cross_sections:?}{guide_curves:?}{path_curve:?}"),SurfaceData::Revolved{revolve_entity,..}=>format!("{revolve_entity:?}"),SurfaceData::Swept{sweep_entity,path_entity,..}=>format!("{sweep_entity:?}{path_entity:?}"),_=>String::new()}
}
pub(crate) fn close(a: &[f64], b: &[f64]) {
    assert_eq!(a.len(), b.len());
    for (i, (&a, &b)) in a.iter().zip(b).enumerate() {
        assert!(
            (a - b).abs() < 1e-11 * a.abs().max(b.abs()).max(1.),
            "surface parameter {i}: {a} != {b}"
        );
    }
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut d = CadDocument::new();
        d.version = version;
        for k in [
            SurfaceKind::Extruded,
            SurfaceKind::Lofted,
            SurfaceKind::Revolved,
            SurfaceKind::Swept,
            SurfaceKind::Nurb,
        ] {
            for control in [false, true] {
                let mut s = Surface::new(k);
                s.acis_data =
                    AcisData::from_sat(&build_box([1., 2., 3.], 4., 5., 6.).to_sat_string());
                setup(&mut s);
                let e = EntityType::Surface(Box::new(s));
                if control {
                    d.add_paper_space_entity(e)?;
                } else {
                    d.add_entity(e)?;
                }
            }
        }
        for binary in [false, true] {
            let p = Path::new(dir).join(format!(
                "{version:?}-{}.dxf",
                if binary { "binary" } else { "ascii" }
            ));
            (if binary {
                DxfWriter::new_binary(&d)
            } else {
                DxfWriter::new(&d)
            })
            .write_to_file(p)?;
        }
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    for e in source.entities() {
        let EntityType::Surface(s) = e else {
            panic!("surface")
        };
        let p = format!("{prefix}.before-{:X}.txt", s.common.handle.value());
        let numbers = fs::read_to_string(p)?
            .split_whitespace()
            .map(|v| v.parse::<f64>().unwrap())
            .collect::<Vec<_>>();
        close(&numbers, &values(s));
        if s.kind != SurfaceKind::Nurb {
            assert!(!profiles(s).contains("Unknown"));
            assert!(profiles(s).contains("Some") || profiles(s).contains("Line"));
        }
    }
    for step in [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13] {
        for format in ["ascii", "binary"] {
            let result = DxfReader::from_file(format!("{prefix}.{step}.{format}.dxf"))?.read()?;
            for (i, e) in source.entities().enumerate() {
                let mut expected = e.clone();
                if i % 2 == 0 {
                    if let EntityType::Surface(s) = &mut expected {
                        change(s);
                    }
                    if step != 0 {
                        expected.apply_transform(&crate::entity_transform::operation(step));
                    }
                }
                let actual = result.get_entity(e.common().handle).unwrap();
                let (EntityType::Surface(a), EntityType::Surface(b)) = (actual, &expected) else {
                    panic!("kind")
                };
                assert_eq!(a.kind, b.kind);
                assert_eq!(a.common.owner_handle, b.common.owner_handle);
                close(&values(a), &values(b));
                assert_eq!(profiles(a), profiles(b), "embedded profiles changed");
                crate::acis::compare(
                    &crate::acis::canonical(&b.acis_data.parse().unwrap()),
                    &crate::acis::canonical(&a.acis_data.parse().unwrap()),
                );
            }
        }
    }
    let copies = fs::read_to_string(format!("{prefix}.copies"))?;
    for encoding in ["ascii", "binary"] {
        let copied = DxfReader::from_file(format!("{prefix}.copies.{encoding}.dxf"))?.read()?;
        assert_eq!(copied.entities().count(), 15);
        for row in copies.lines() {
            let handles: Vec<_> = row
                .split_whitespace()
                .map(|v| acadrust::Handle::new(u64::from_str_radix(v, 16).unwrap()))
                .collect();
            let EntityType::Surface(a) = source.get_entity(handles[0]).unwrap() else {
                panic!("source")
            };
            let EntityType::Surface(b) = copied.get_entity(handles[1]).unwrap() else {
                panic!("copy")
            };
            assert_eq!(a.kind, b.kind);
            close(&values(a), &values(b));
            assert_eq!(profiles(a), profiles(b));
            assert!(copied
                .block_records
                .iter()
                .any(|r| r.name == "*Paper_Space" && r.handle == b.common.owner_handle));
            crate::acis::compare(
                &crate::acis::canonical(&a.acis_data.parse().unwrap()),
                &crate::acis::canonical(&b.acis_data.parse().unwrap()),
            );
        }
        let deleted = DxfReader::from_file(format!("{prefix}.deleted.{encoding}.dxf"))?.read()?;
        assert_eq!(deleted.entities().count(), source.entities().count());
        for original in source.entities() {
            assert_eq!(
                copied.get_entity(original.common().handle).unwrap(),
                original
            );
            assert_eq!(
                deleted.get_entity(original.common().handle).unwrap(),
                original
            );
        }
    }
    let created = DxfReader::from_file(format!("{prefix}.created.dxf"))?.read()?;
    assert_eq!(created.entities().count(), 5);
    for e in created.entities() {
        let EntityType::Surface(s) = e else {
            panic!("kind")
        };
        assert!(s.acis_data.parse().unwrap().records.len() > 10);
    }
    println!("Specialized surfaces: parameters, embedded profiles, placement, neighbors and creation match Rust");
    Ok(())
}
