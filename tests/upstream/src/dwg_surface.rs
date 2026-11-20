// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{AcisData, EmbeddedEntity, Surface, SurfaceData, SurfaceKind};
use acadrust::objects::ObjectType;
use acadrust::{CadDocument, DwgReader, DwgWriter, DxfVersion, EntityType, Vector3};
use std::{fs, path::Path};
fn profiles(s: &mut Surface, mut f: impl FnMut(&mut EmbeddedEntity)) {
    match &mut s.surface_data {
        SurfaceData::Extruded { sweep_entity, .. } => {
            if let Some(e) = sweep_entity {
                f(e)
            }
        }
        SurfaceData::Revolved { revolve_entity, .. } => {
            if let Some(e) = revolve_entity {
                f(e)
            }
        }
        SurfaceData::Swept {
            sweep_entity,
            path_entity,
            ..
        } => {
            if let Some(e) = sweep_entity {
                f(e)
            }
            if let Some(e) = path_entity {
                f(e)
            }
        }
        SurfaceData::Lofted {
            cross_section_entities,
            guide_entities,
            path_entity,
            ..
        } => {
            for e in cross_section_entities.iter_mut().chain(guide_entities) {
                f(e)
            }
            if let Some(e) = path_entity {
                f(e)
            }
        }
        _ => {}
    }
}
fn normalized(mut s: Surface) -> Surface {
    profiles(&mut s, |e| {
        if let EmbeddedEntity::Body { acis_data, .. } = e {
            if let Some(d) = acis_data.parse() {
                *acis_data = AcisData::from_sat(&crate::acis::canonical(&d).to_sat_string());
            }
        }
    });
    s
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1018,
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut d = CadDocument::new();
        d.version = version;
        for kind in [
            SurfaceKind::Generic,
            SurfaceKind::Plane,
            SurfaceKind::Extruded,
            SurfaceKind::Lofted,
            SurfaceKind::Revolved,
            SurfaceKind::Swept,
            SurfaceKind::Nurb,
        ] {
            for control in [false, true] {
                let mut s = Surface::new(kind);
                if kind != SurfaceKind::Generic && kind != SurfaceKind::Plane {
                    super::surface::setup(&mut s);
                }
                profiles(&mut s, |e| {
                    if let EmbeddedEntity::Body { type_code, .. } = e {
                        *type_code = 15;
                    }
                });
                s.acis_data = AcisData::from_sat(
                    &acadrust::entities::acis::primitives::build_box([1., 2., 3.], 4., 5., 6.)
                        .to_sat_string(),
                );
                s.u_isolines = 3;
                s.v_isolines = 5;
                if control {
                    d.add_paper_space_entity(EntityType::Surface(Box::new(s)))?;
                } else {
                    d.add_entity(EntityType::Surface(Box::new(s)))?;
                }
            }
        }
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version:?}.dwg")), &d)?;
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    let handles: Vec<u64> = fs::read_to_string(format!("{prefix}.edited"))?
        .split_whitespace()
        .map(|s| s.parse().unwrap())
        .collect();
    assert_eq!(source.entities().count(), 14);
    assert_eq!(handles.len(), 7);
    for e in source.entities() {
        let EntityType::Surface(s) = e else {
            panic!("surface")
        };
        let numbers = fs::read_to_string(format!("{prefix}.before-{}", s.common.handle.value()))?
            .split_whitespace()
            .map(|x| x.parse::<f64>().unwrap())
            .collect::<Vec<_>>();
        super::surface::close(&numbers, &super::surface::values(s));
    }
    for step in ["same", "edited", "cleared", "refilled"] {
        let actual = DwgReader::from_file(format!("{prefix}.{step}.dwg"))?.read()?;
        assert_eq!(actual.entities().count(), 14);
        for (h, o) in &source.objects {
            let fallback=source.objects.values().any(|v|matches!(v,ObjectType::Dictionary(d) if d.get("CADCODEC_LOFT_CURVES_V1")==Some(*h)));
            if fallback {
                let ObjectType::XRecord(r) = &actual.objects[h] else {
                    panic!("fallback")
                };
                assert_eq!(r.entries.len(), 1, "Body profile fallback disabled");
                assert_eq!(r.entries[0].code, 90);
                assert_eq!(r.entries[0].value.as_i32(), Some(1));
            } else {
                assert_eq!(
                    actual.objects.get(h),
                    Some(o),
                    "Unrelated metadata unchanged"
                );
            }
        }
        for e in source.entities() {
            let EntityType::Surface(old) = e else {
                panic!("surface")
            };
            let EntityType::Surface(got) = actual.get_entity(e.common().handle).unwrap() else {
                panic!("surface")
            };
            let mut expected = normalized((**old).clone());
            let got = normalized((**got).clone());
            if handles.contains(&old.common.handle.value()) && step != "same" {
                let original = expected.surface_data.clone();
                if expected.kind != SurfaceKind::Generic && expected.kind != SurfaceKind::Plane {
                    super::surface::change(&mut expected);
                }
                expected.u_isolines = 13;
                expected.v_isolines = 17;
                if expected.kind == SurfaceKind::Nurb && source.version < DxfVersion::AC1027 {
                    expected.surface_data = original;
                }
                profiles(&mut expected, |e| {
                    if let EmbeddedEntity::Line(l) = e {
                        l.start = Vector3::new(11., 12., 13.);
                    }
                });
            }
            assert_eq!(
                got.surface_data, expected.surface_data,
                "{step}: {:?}",
                old.kind
            );
            assert_eq!(
                (got.u_isolines, got.v_isolines),
                (expected.u_isolines, expected.v_isolines)
            );
            let mut common = expected.common.clone();
            let edited = handles.contains(&old.common.handle.value());
            if edited && source.version >= DxfVersion::AC1027 {
                common.has_ds_data = step != "cleared";
            }
            assert_eq!(got.common, common);
            if edited && step == "cleared" {
                assert!(!got.acis_data.has_data());
            } else {
                let shape = if edited && step == "refilled" {
                    acadrust::entities::acis::primitives::build_sphere([3., 4., 5.], 6.)
                } else {
                    old.acis_data.parse().unwrap()
                };
                super::acis::compare(
                    &super::acis::canonical(&shape),
                    &super::acis::canonical(&got.acis_data.parse().unwrap()),
                );
            }
        }
    }
    let fallback = DwgReader::from_file(format!("{prefix}.fallback.dwg"))?.read()?;
    let h = acadrust::Handle::new(
        fs::read_to_string(format!("{prefix}.fallback-id"))?
            .parse()
            .unwrap(),
    );
    let EntityType::Surface(s) = fallback.get_entity(h).unwrap() else {
        panic!("surface")
    };
    let SurfaceData::Lofted {
        cross_section_entities,
        guide_entities,
        path_entity,
        ..
    } = &s.surface_data
    else {
        panic!("loft")
    };
    assert_eq!(cross_section_entities.len(), 1);
    assert!(guide_entities.is_empty() && path_entity.is_none());
    let EmbeddedEntity::Line(line) = &cross_section_entities[0] else {
        panic!("line")
    };
    assert_eq!(line.start, Vector3::new(1., 2., 3.));
    assert_eq!(line.end, Vector3::new(4., 5., 6.));
    let empty = DwgReader::from_file(format!("{prefix}.empty-profiles.dwg"))?.read()?;
    for e in empty.entities() {
        let EntityType::Surface(s) = e else {
            panic!("surface")
        };
        if s.kind == SurfaceKind::Lofted && handles.contains(&s.common.handle.value()) {
            let SurfaceData::Lofted {
                cross_section_entities,
                guide_entities,
                path_entity,
                ..
            } = &s.surface_data
            else {
                panic!("loft")
            };
            assert!(
                cross_section_entities.is_empty()
                    && guide_entities.is_empty()
                    && path_entity.is_none(),
                "Cleared inputs must not return from stale XRECORD"
            );
        } else {
            assert_eq!(
                Some(e),
                DwgReader::from_file(format!("{prefix}.edited.dwg"))?
                    .read()?
                    .get_entity(e.common().handle)
            );
        }
    }
    println!("DWG surfaces: seven classes, parameter/profile edits, empty/refill and control entities match Rust");
    Ok(())
}
