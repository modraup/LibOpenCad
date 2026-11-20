// SPDX-License-Identifier: MPL-2.0
use crate::acis::{canonical, compare};
use acadrust::entities::acis::{primitives::*, SatDocument};
use acadrust::entities::solid3d::{AcisMaterial, Silhouette, Wire};
use acadrust::entities::{AcisData, Body, Region, Solid3D};
use acadrust::{CadDocument, Color, DwgReader, DwgWriter, DxfVersion, EntityType, Handle, Vector3};
use std::{fmt::Write, fs, path::Path};
fn fields(e: &EntityType) -> Option<(&AcisData, &Vec<Wire>, &Vec<Silhouette>, Vector3, u64, i32)> {
    match e {
        EntityType::Solid3D(e) => Some((
            &e.acis_data,
            &e.wires,
            &e.silhouettes,
            e.point_of_reference,
            e.history_handle.unwrap_or(Handle::NULL).value(),
            38,
        )),
        EntityType::Region(e) => Some((
            &e.acis_data,
            &e.wires,
            &e.silhouettes,
            e.point_of_reference,
            0,
            37,
        )),
        EntityType::Body(e) => Some((
            &e.acis_data,
            &e.wires,
            &e.silhouettes,
            e.point_of_reference,
            0,
            39,
        )),
        _ => None,
    }
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1012,
        DxfVersion::AC1014,
        DxfVersion::AC1015,
        DxfVersion::AC1018,
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut doc = CadDocument::new();
        doc.version = version;
        for i in 0..9 {
            let shape = match i % 3 {
                0 => build_box([1. + i as f64, 2., 3.], 4., 6., 8.),
                1 => build_sphere([-3., i as f64, 7.], 4.),
                _ => build_cylinder([2., 3., i as f64], 3., 8.),
            };
            let sat = if i >= 6 {
                String::new()
            } else {
                shape.to_sat_string()
            };
            let mut e = match i % 3 {
                0 => EntityType::Solid3D(Solid3D::from_sat(&sat)),
                1 => EntityType::Region(Region::from_sat(&sat)),
                _ => EntityType::Body(Body::from_sat(&sat)),
            };
            let (data, wires, silhouettes, point) = match &mut e {
                EntityType::Solid3D(e) => (
                    &mut e.acis_data,
                    &mut e.wires,
                    &mut e.silhouettes,
                    &mut e.point_of_reference,
                ),
                EntityType::Region(e) => (
                    &mut e.acis_data,
                    &mut e.wires,
                    &mut e.silhouettes,
                    &mut e.point_of_reference,
                ),
                EntityType::Body(e) => (
                    &mut e.acis_data,
                    &mut e.wires,
                    &mut e.silhouettes,
                    &mut e.point_of_reference,
                ),
                _ => unreachable!(),
            };
            *point = Vector3::new(i as f64 + 1., 2., 3.);
            data.wireframe_isolines = 13 + i;
            data.wireframe_data_present = true;
            data.wireframe_point_present = true;
            data.revision.has_guid = true;
            data.revision.major = 123456789;
            data.revision.minor1 = -7;
            data.revision.minor2 = 29;
            data.revision.bytes = [1, 2, 3, 4, 128, 129, 254, 255];
            data.revision.end_marker = 42;
            if i < 6 && version >= DxfVersion::AC1021 {
                data.materials.push(AcisMaterial {
                    array_index: 3,
                    absolute_reference: 7,
                    material_handle: Some(doc.header.model_space_block_handle),
                });
            }
            if i == 3 {
                data.extra_acis_data = Some(Box::new(AcisData::from_sat(
                    &build_sphere([7., 8., 9.], 2.).to_sat_string(),
                )));
            }
            let mut wire = Wire::new();
            wire.acis_index = 8;
            wire.selection_marker = 19;
            wire.color = Color::Index(4);
            wire.points = vec![Vector3::new(1., 2., 3.), Vector3::new(4., 5., 6.)];
            wire.has_transform = true;
            wire.translation = Vector3::new(3., -2., 1.);
            wire.scale = Vector3::new(2., 2., 2.);
            wire.has_rotation = true;
            wire.has_reflection = true;
            wires.push(wire.clone());
            let mut sil = Silhouette::new(17);
            sil.target = Vector3::new(3., 2., 1.);
            sil.is_perspective = true;
            sil.add_wire(wire);
            silhouettes.push(sil);
            if i == 5 || i == 8 {
                *point = Vector3::ZERO;
                wires.clear();
                silhouettes.clear();
                data.wireframe_data_present = false;
                data.wireframe_point_present = false;
                data.wireframe_isoline_present = false;
                data.wireframe_isolines = 0;
            }
            let h = if i % 2 == 0 {
                doc.add_entity(e)?
            } else {
                doc.add_paper_space_entity(e)?
            };
            if i < 6 && i % 3 == 0 && version >= DxfVersion::AC1021 {
                use acadrust::objects::{SolidHistoryOperation, SolidHistorySphere};
                doc.create_solid_history(
                    h,
                    SolidHistoryOperation::Sphere(SolidHistorySphere {
                        radius: 4.,
                        ..Default::default()
                    }),
                )
                .unwrap();
            }
        }
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version:?}.dwg")), &doc)?;
        if version >= DxfVersion::AC1027 {
            let mut empty = CadDocument::new();
            empty.version = version;
            for i in 0..9 {
                let e = match i % 3 {
                    0 => EntityType::Solid3D(Solid3D::new()),
                    1 => EntityType::Region(Region::new()),
                    _ => EntityType::Body(Body::new()),
                };
                if i % 2 == 0 {
                    empty.add_entity(e)?;
                } else {
                    empty.add_paper_space_entity(e)?;
                }
            }
            DwgWriter::write_to_file(
                Path::new(dir).join(format!("{version:?}-empty.dwg")),
                &empty,
            )?;
        }
    }
    Ok(())
}
pub fn verify(file: &str, prefix: &str) -> acadrust::Result<()> {
    let doc = DwgReader::from_file(file)?.read()?;
    let mut rows = Vec::new();
    for e in doc.entities() {
        let Some((a, wires, sils, point, history, kind)) = fields(e) else {
            continue;
        };
        let h = e.common().handle.value();
        let mut row = format!(
            "{h} {kind} {} {history} {} {} {} {} {} {} {} {} {} {} {}",
            e.common().owner_handle.value(),
            a.version as u8,
            a.is_binary as u8,
            a.wireframe_data_present as u8,
            a.wireframe_point_present as u8,
            a.wireframe_isoline_present as u8,
            a.acis_empty_bit as u8,
            a.wireframe_isolines,
            wires.len(),
            sils.len(),
            a.materials.len(),
            a.revision.has_guid as u8
        );
        for n in [point.x, point.y, point.z] {
            write!(row, " {}", n.to_bits()).unwrap();
        }
        write!(
            row,
            " {} {} {} {}",
            a.revision.major, a.revision.minor1, a.revision.minor2, a.revision.end_marker
        )
        .unwrap();
        for n in a.revision.bytes {
            write!(row, " {n}").unwrap();
        }
        for m in &a.materials {
            write!(
                row,
                " {} {} {}",
                m.array_index,
                m.absolute_reference,
                m.material_handle.unwrap_or(Handle::NULL).value()
            )
            .unwrap();
        }
        for wire in wires.iter().chain(sils.iter().flat_map(|s| &s.wires)) {
            write!(
                row,
                " {} {} {} {} {} {} {} {}",
                wire.wire_type as u8,
                wire.selection_marker,
                wire.color.index().unwrap_or(256),
                wire.acis_index,
                wire.has_transform as u8,
                wire.has_rotation as u8,
                wire.has_reflection as u8,
                wire.has_shear as u8
            )
            .unwrap();
            for p in wire.points.iter().chain([
                &wire.x_axis,
                &wire.y_axis,
                &wire.z_axis,
                &wire.translation,
                &wire.scale,
            ]) {
                for n in [p.x, p.y, p.z] {
                    write!(row, " {}", n.to_bits()).unwrap();
                }
            }
        }
        for s in sils {
            write!(
                row,
                " {} {} {}",
                s.viewport_id, s.is_perspective as u8, s.has_wires as u8
            )
            .unwrap();
            for p in [s.target, s.view_direction, s.up_vector] {
                for n in [p.x, p.y, p.z] {
                    write!(row, " {}", n.to_bits()).unwrap();
                }
            }
        }
        rows.push(row);
        if let Some(g) = a.parse() {
            compare(
                &canonical(&g),
                &SatDocument::parse(&fs::read_to_string(format!("{prefix}.{h}.sat"))?).unwrap(),
            );
        } else {
            assert!(!Path::new(&format!("{prefix}.{h}.sat")).exists());
        }
        if let Some(extra) = &a.extra_acis_data {
            compare(
                &canonical(&extra.parse().unwrap()),
                &SatDocument::parse(&fs::read_to_string(format!("{prefix}.{h}.extra.sat"))?)
                    .unwrap(),
            );
        }
    }
    rows.sort();
    let cpp = fs::read_to_string(format!("{prefix}.txt"))?;
    let mut actual: Vec<_> = cpp.lines().collect();
    actual.sort();
    assert_eq!(actual, rows);
    assert_eq!(rows.len(), 9);
    let saved = DwgReader::from_file(format!("{prefix}.dwg"))?.read()?;
    assert_eq!(
        doc.entities().collect::<Vec<_>>(),
        saved.entities().collect::<Vec<_>>()
    );
    let edited = DwgReader::from_file(format!("{prefix}.edited.dwg"))?.read()?;
    assert_eq!(
        doc.objects, edited.objects,
        "History and unrelated objects must remain unchanged"
    );
    for e in doc.entities() {
        let Some((before, _, _, _, _, _)) = fields(e) else {
            continue;
        };
        let h = e.common().handle;
        let actual = edited.get_entity(h).unwrap();
        if h.value() % 2 == 0 {
            let (a, _, _, _, _, _) = fields(actual).unwrap();
            let mut shape = build_torus([9., -4., 7.], 8., 2.);
            if doc.version <= DxfVersion::AC1015 {
                shape.header.version = acadrust::entities::acis::SatVersion::V4_0;
            }
            compare(&canonical(&a.parse().unwrap()), &canonical(&shape));
            let mut expected = e.clone();
            if doc.version >= DxfVersion::AC1027 {
                expected.common_mut().has_ds_data = true;
            }
            match &mut expected {
                EntityType::Solid3D(v) => {
                    v.acis_data = a.clone();
                    v.point_of_reference = fields(actual).unwrap().3;
                }
                EntityType::Region(v) => {
                    v.acis_data = a.clone();
                    v.point_of_reference = fields(actual).unwrap().3;
                }
                EntityType::Body(v) => {
                    v.acis_data = a.clone();
                    v.point_of_reference = fields(actual).unwrap().3;
                }
                _ => unreachable!(),
            };
            assert_eq!(&expected, actual);
            assert_eq!(before.materials, a.materials);
            assert_eq!(before.revision, a.revision);
            assert_eq!(before.extra_acis_data, a.extra_acis_data);
        } else {
            assert_eq!(e, actual);
        }
    }
    if doc.version >= DxfVersion::AC1027 {
        let paged = DwgReader::from_file(format!("{prefix}.paged.dwg"))?.read()?;
        assert_eq!(
            doc.entities().collect::<Vec<_>>(),
            paged.entities().collect::<Vec<_>>()
        );
        assert_eq!(doc.objects, paged.objects);
        let changed = DwgReader::from_file(format!("{prefix}.paged-edited.dwg"))?.read()?;
        assert_eq!(doc.objects, changed.objects);
        let first = doc
            .entities()
            .filter(|e| fields(e).is_some())
            .map(|e| e.common().handle)
            .min()
            .unwrap();
        for e in doc.entities() {
            let actual = changed.get_entity(e.common().handle).unwrap();
            if e.common().handle != first {
                assert_eq!(e, actual);
                continue;
            }
            let (a, _, _, _, _, _) = fields(actual).unwrap();
            compare(
                &canonical(&a.parse().unwrap()),
                &canonical(&build_torus([9., -4., 7.], 8., 2.)),
            );
            let mut expected = e.clone();
            if let EntityType::Solid3D(v) = &mut expected {
                let before = v.acis_data.clone();
                v.acis_data = a.clone();
                v.point_of_reference = fields(actual).unwrap().3;
                assert_eq!(before.materials, a.materials);
                assert_eq!(before.revision, a.revision);
                assert_eq!(before.extra_acis_data, a.extra_acis_data);
            } else {
                panic!();
            }
            assert_eq!(&expected, actual);
        }
    }
    let cleared = DwgReader::from_file(format!("{prefix}.cleared.dwg"))?.read()?;
    assert_eq!(doc.objects, cleared.objects);
    for e in doc.entities() {
        let a = cleared.get_entity(e.common().handle).unwrap();
        if e.common().handle.value() % 2 != 0 {
            assert_eq!(e, a);
            continue;
        }
        let before = fields(e).unwrap().0;
        let after = fields(a).unwrap().0;
        assert!(after.parse().is_none());
        assert!(after.materials.is_empty());
        assert_eq!(before.revision, after.revision);
        assert_eq!(before.extra_acis_data, after.extra_acis_data);
        assert_eq!(before.wireframe_data_present, after.wireframe_data_present);
        assert_eq!(
            before.wireframe_point_present,
            after.wireframe_point_present
        );
        assert_eq!(
            before.wireframe_isoline_present,
            after.wireframe_isoline_present
        );
        assert_eq!(before.wireframe_isolines, after.wireframe_isolines);
        let mut expected = e.clone();
        expected.common_mut().has_ds_data = false;
        match &mut expected {
            EntityType::Solid3D(v) => {
                v.acis_data = after.clone();
                v.point_of_reference = fields(a).unwrap().3;
            }
            EntityType::Region(v) => {
                v.acis_data = after.clone();
                v.point_of_reference = fields(a).unwrap().3;
            }
            EntityType::Body(v) => {
                v.acis_data = after.clone();
                v.point_of_reference = fields(a).unwrap().3;
            }
            _ => unreachable!(),
        };
        assert_eq!(&expected, a);
    }
    Ok(())
}

pub fn verify_empty(file: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(file)?.read()?;
    let mut ids: Vec<_> = source.entities().map(|e| e.common().handle).collect();
    ids.sort();
    for (step, h) in ids.iter().enumerate() {
        let actual = DwgReader::from_file(format!("{prefix}.filled-{}.dwg", h.value()))?.read()?;
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.layers, actual.layers);
        for (i, id) in ids.iter().enumerate() {
            let e = source.get_entity(*id).unwrap();
            let a = actual.get_entity(*id).unwrap();
            if i > step {
                assert_eq!(e, a);
                continue;
            }
            let before = fields(e).unwrap().0;
            let after = fields(a).unwrap().0;
            compare(
                &canonical(&after.parse().unwrap()),
                &canonical(&build_sphere([1., 2., 3.], 2. + (id.value() % 3) as f64)),
            );
            assert_eq!(before.revision, after.revision);
            assert_eq!(before.materials, after.materials);
            assert_eq!(before.extra_acis_data, after.extra_acis_data);
            assert_eq!(before.wireframe_data_present, after.wireframe_data_present);
            assert_eq!(before.wireframe_isolines, after.wireframe_isolines);
            let mut expected = e.clone();
            expected.common_mut().has_ds_data = true;
            match &mut expected {
                EntityType::Solid3D(v) => {
                    v.acis_data = after.clone();
                    v.point_of_reference = fields(a).unwrap().3;
                }
                EntityType::Region(v) => {
                    v.acis_data = after.clone();
                    v.point_of_reference = fields(a).unwrap().3;
                }
                EntityType::Body(v) => {
                    v.acis_data = after.clone();
                    v.point_of_reference = fields(a).unwrap().3;
                }
                _ => unreachable!(),
            };
            assert_eq!(&expected, a);
        }
    }
    let cleared = DwgReader::from_file(format!("{prefix}.cleared-all.dwg"))?.read()?;
    assert_eq!(
        source.entities().collect::<Vec<_>>(),
        cleared.entities().collect::<Vec<_>>()
    );
    let refilled = DwgReader::from_file(format!("{prefix}.refilled.dwg"))?.read()?;
    let first = DwgReader::from_file(format!("{prefix}.filled-{}.dwg", ids[0].value()))?.read()?;
    assert_eq!(
        first.entities().collect::<Vec<_>>(),
        refilled.entities().collect::<Vec<_>>()
    );
    Ok(())
}
