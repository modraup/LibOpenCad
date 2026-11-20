// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{AcisData, EmbeddedEntity, Line, SurfaceData};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
use std::{fs, path::Path};
fn surfaces(d: &mut CadDocument, mut f: impl FnMut(usize, &mut SurfaceData)) {
    for e in d.entities_mut() {
        if let EntityType::Surface(s) = e {
            f(s.common.handle.value() as usize, &mut s.surface_data);
        }
    }
}
fn body() -> EmbeddedEntity {
    EmbeddedEntity::Body {
        type_code: 77,
        acis_data: AcisData::from_sat("700 0 0 0\nwire $-1 -1 $-1 $-1 #\n"),
    }
}
fn assign(v: &mut SurfaceData, mode: &str) {
    let mut p = super::history_profiles::profiles();
    if let EmbeddedEntity::Unknown { bit_count, .. } = &mut p[11] {
        *bit_count = 13;
    }
    let one = |index: usize| {
        if mode == "clear" || mode == "legacy" {
            None
        } else if mode == "body" {
            Some(body())
        } else {
            Some(p[index].clone())
        }
    };
    match v {
        SurfaceData::Extruded { sweep_entity, .. } => *sweep_entity = one(5),
        SurfaceData::Revolved {
            revolve_entity,
            class_version,
            entity_id,
            ..
        } => {
            *revolve_entity = one(2);
            *class_version = if mode == "legacy" { 7 } else { 0 };
            *entity_id = if mode == "legacy" { 42 } else { 0 };
        }
        SurfaceData::Swept {
            sweep_entity,
            path_entity,
            class_version,
            ..
        } => {
            *sweep_entity = one(6);
            *path_entity = one(8);
            *class_version = if mode == "legacy" { 11 } else { 0 };
            if mode == "legacy" {
                *sweep_entity = Some(body());
                *path_entity = Some(p[1].clone());
            }
        }
        SurfaceData::Lofted {
            cross_section_entities,
            guide_entities,
            path_entity,
            ..
        } => {
            *cross_section_entities = if mode == "clear" || mode == "legacy" {
                vec![]
            } else if mode == "body" {
                vec![body(), body()]
            } else {
                p.clone()
            };
            *guide_entities = if mode == "clear" || mode == "legacy" {
                vec![]
            } else if mode == "body" {
                vec![body()]
            } else {
                vec![p[7].clone(), p[9].clone()]
            };
            *path_entity = one(3);
        }
        _ => {}
    }
}
fn change(v: &mut SurfaceData) {
    let edit = super::history_profiles::change;
    match v {
        SurfaceData::Extruded { sweep_entity, .. } => {
            sweep_entity.as_mut().map(edit);
        }
        SurfaceData::Revolved { revolve_entity, .. } => {
            revolve_entity.as_mut().map(edit);
        }
        SurfaceData::Swept {
            sweep_entity,
            path_entity,
            ..
        } => {
            sweep_entity.as_mut().map(edit);
            path_entity.as_mut().map(edit);
        }
        SurfaceData::Lofted {
            cross_section_entities,
            guide_entities,
            path_entity,
            ..
        } => {
            cross_section_entities.iter_mut().for_each(edit);
            guide_entities.iter_mut().for_each(edit);
            path_entity.as_mut().map(edit);
            // Removal, append and reordering must preserve record boundaries.
            cross_section_entities.remove(0);
            cross_section_entities.swap(0, 1);
            cross_section_entities.push(EmbeddedEntity::Line(Line::from_points(
                Vector3::new(9., 8., 7.),
                Vector3::new(6., 5., 4.),
            )));
            guide_entities.reverse();
        }
        _ => {}
    }
}
fn save(d: &CadDocument, path: impl AsRef<Path>, binary: bool) -> acadrust::Result<()> {
    (if binary {
        DxfWriter::new_binary(d)
    } else {
        DxfWriter::new(d)
    })
    .write_to_file(path)
}
pub fn generate(source: &str, out: &str) -> acadrust::Result<()> {
    fs::create_dir_all(out)?;
    for version in ["AC1021", "AC1024", "AC1027", "AC1032"] {
        let mut d =
            DxfReader::from_file(Path::new(source).join(format!("{version}-ascii.dxf")))?.read()?;
        let h = d.add_entity(EntityType::Line(Line::from_points(
            Vector3::new(1., 2., 3.),
            Vector3::new(4., 5., 6.),
        )))?;
        surfaces(&mut d, |_, v| {
            assign(v, "curves");
            if let SurfaceData::Lofted {
                cross_sections,
                guide_curves,
                path_curve,
                ..
            } = v
            {
                *cross_sections = vec![h, h];
                *guide_curves = vec![h];
                *path_curve = Some(h);
            }
        });
        for binary in [false, true] {
            save(
                &d,
                Path::new(out).join(format!(
                    "{version}-{}.dxf",
                    if binary { "binary" } else { "ascii" }
                )),
                binary,
            )?;
        }
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    for mode in ["edited", "body", "clear", "legacy"] {
        let mut expected = source.clone();
        surfaces(&mut expected, |i, v| {
            if i % 2 == 0 {
                if mode == "edited" {
                    change(v)
                } else {
                    assign(v, mode)
                }
            }
        });
        for binary in [false, true] {
            let ext = if binary { "binary" } else { "ascii" };
            let oracle = format!("{prefix}.{mode}.oracle.{ext}.dxf");
            save(&expected, &oracle, binary)?;
            let normalized = DxfReader::from_file(&oracle)?.read()?;
            let actual = DxfReader::from_file(format!("{prefix}.{mode}.{ext}.dxf"))?.read()?;
            assert_eq!(actual.entities().count(), source.entities().count());

            for e in source.entities() {
                let i = e.common().handle.value();
                let got = actual.get_entity(e.common().handle).unwrap();
                super::history_author::compare(e.common(), got.common());
                if let EntityType::Surface(s) = e {
                    let EntityType::Surface(a) = got else {
                        panic!("surface kind")
                    };
                    // Rust writing fills an empty loft from database references and drops the old sweep header.
                    // Compare those edits to its in-memory representation instead.
                    let want = if i % 2 == 1 {
                        &source
                    } else if mode == "clear" || mode == "legacy" {
                        &expected
                    } else {
                        &normalized
                    };
                    let EntityType::Surface(b) = want.get_entity(e.common().handle).unwrap() else {
                        unreachable!()
                    };
                    super::history_author::compare(&a.surface_data, &b.surface_data);
                    assert_eq!(a.kind, s.kind);
                    assert_eq!(a.u_isolines, s.u_isolines);
                    assert_eq!(a.v_isolines, s.v_isolines);
                    assert_eq!(a.history_handle, s.history_handle);
                    super::acis::compare(
                        &super::acis::canonical(&a.acis_data.parse().unwrap()),
                        &super::acis::canonical(&s.acis_data.parse().unwrap()),
                    );
                } else {
                    super::history_author::compare(e, got);
                }
            }
        }
    }
    // Independently reproduce metadata-only edit expectations. Its codec is R2018,
    // while older opaque bodies cannot be transcoded safely by the native API.
    let mut fallback = source.clone();
    surfaces(&mut fallback, |i, v| {
        if i % 2 == 0 {
            if let SurfaceData::Lofted {
                cross_section_entities,
                ..
            } = v
            {
                if source.version != acadrust::DxfVersion::AC1032 {
                    cross_section_entities.retain(|v| !matches!(v, EmbeddedEntity::Unknown { .. }));
                }
                if let EmbeddedEntity::Point(p) = &mut cross_section_entities[0] {
                    p.location.x += 2.;
                } else {
                    panic!("point");
                }
            }
        }
    });
    for binary in [false, true] {
        let ext = if binary { "binary" } else { "ascii" };
        let oracle = format!("{prefix}.fallback.oracle.{ext}.dxf");
        save(&fallback, &oracle, binary)?;
        let expected = DxfReader::from_file(&oracle)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.fallback.{ext}.dxf"))?.read()?;
        for e in source.entities() {
            if let EntityType::Surface(s) = e {
                if matches!(s.surface_data, SurfaceData::Lofted { .. })
                    && s.common.handle.value() % 2 == 0
                {
                    let EntityType::Surface(a) = actual.get_entity(s.common.handle).unwrap() else {
                        unreachable!()
                    };
                    let EntityType::Surface(b) = expected.get_entity(s.common.handle).unwrap()
                    else {
                        unreachable!()
                    };
                    super::history_author::compare(&a.surface_data, &b.surface_data);
                }
            }
        }
    }
    Ok(())
}
