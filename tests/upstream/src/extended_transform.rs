// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::{Entity, EntityCommon, ExtendedEntity, ExtendedEntityData as E};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let mut entities: Vec<_> = crate::extended_entities::entities()
        .into_iter()
        .filter(|e| !matches!(e, EntityType::Hatch(_)))
        .collect();
    entities.extend(crate::more_extended::items().into_iter().map(|data| {
        EntityType::Extended(Box::new(ExtendedEntity {
            common: EntityCommon::new(),
            data,
        }))
    }));
    for e in entities {
        for variant in 0..3 {
            let mut e = e.clone();
            if variant == 1 {
                match &mut e {
                    EntityType::Light(e) => {
                        e.photometric_mode = false;
                        e.photometric_data = None;
                    }
                    EntityType::Extended(e) => match &mut e.data {
                        E::ArcAlignedText(v) => v.normal = Vector3::new(1., 2., 3.),
                        E::RemoteText(v) => v.normal = Vector3::new(-1., 2., 1.),
                        _ => {}
                    },
                    _ => {}
                }
            }
            use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
            e.common_mut().extended_data.add_record(ExtendedDataRecord {
                application_name: "Geometry".into(),
                values: vec![X::Position3D(Vector3::new(1., 2., 3.)), X::Distance(2.5)],
            });
            if variant == 2 {
                e.common_mut().layer = "Control".into();
                doc.add_paper_space_entity(e)?;
            } else {
                doc.add_entity(e)?;
            }
        }
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    // LIGHT writer emits a double at integer code 63. Use the typed binary pair
    // writer as in the existing upstream fixture, with integral initial Z.
    use acadrust::io::dxf::{DxfBinaryWriter, DxfStreamWriter};
    let text = std::fs::read_to_string(ascii)?;
    let mut lines = text.lines();
    let mut out = DxfBinaryWriter::new(std::fs::File::create(binary)?)?;
    while let Some(code) = lines.next() {
        let code: i32 = code.trim().parse().unwrap();
        let value = lines.next().unwrap();
        let value = if code == 63 {
            (value.trim().parse::<f64>().unwrap() as i16).to_string()
        } else {
            value.into()
        };
        out.write_string(code, &value)?;
    }
    Ok(())
}
macro_rules! points {($a:ident,$b:ident,$($f:ident),*)=>{$(point($a.$f,$b.$f);$a.$f=$b.$f;)*};}
macro_rules! numbers {($a:ident,$b:ident,$($f:ident),*)=>{$(near($a.$f,$b.$f);$a.$f=$b.$f;)*};}
fn pairs(a: &mut [Vector3], b: &[Vector3]) {
    assert_eq!(a.len(), b.len());
    for (a, b) in a.iter_mut().zip(b) {
        point(*a, *b);
        *a = *b;
    }
}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Light(a), EntityType::Light(b)) => {
            points!(a, b, position, target);
            if let (Some(a), Some(b)) = (&mut a.photometric_data, &b.photometric_data) {
                points!(a, b, web_rotation);
            }
        }
        (EntityType::SectionSymbol(a), EntityType::SectionSymbol(b)) => {
            assert_eq!(a.points.len(), b.points.len());
            for (a, b) in a.points.iter_mut().zip(&b.points) {
                points!(a, b, point, label_offset);
            }
            for (a, b) in a
                .end_a
                .iter_mut()
                .chain(&mut a.end_b)
                .zip(b.end_a.iter().chain(&b.end_b))
            {
                near(*a, *b);
                *a = *b;
            }
            numbers!(a, b, tick_a, tick_b);
        }
        (EntityType::ViewBorder(a), EntityType::ViewBorder(b)) => {
            for (a, b) in a
                .min
                .iter_mut()
                .chain(&mut a.max)
                .chain(&mut a.center)
                .zip(b.min.iter().chain(&b.max).chain(&b.center))
            {
                near(*a, *b);
                *a = *b;
            }
            numbers!(a, b, rotation_angle);
        }
        (EntityType::Extended(a), EntityType::Extended(b)) => match (&mut a.data, &b.data) {
            (E::SectionObject(a), E::SectionObject(b)) => {
                pairs(&mut a.vertices, &b.vertices);
                pairs(&mut a.back_line_vertices, &b.back_line_vertices);
                points!(a, b, vertical_direction);
            }
            (E::ArcAlignedText(a), E::ArcAlignedText(b)) => {
                points!(a, b, center, normal);
                numbers!(
                    a,
                    b,
                    radius,
                    text_size,
                    character_spacing,
                    offset_from_arc,
                    right_offset,
                    left_offset,
                    start_angle,
                    end_angle
                );
            }
            (E::RemoteText(a), E::RemoteText(b)) => {
                points!(a, b, position, normal);
                numbers!(a, b, rotation, height);
            }
            (E::GeoPositionMarker(a), E::GeoPositionMarker(b)) => {
                points!(a, b, position);
                numbers!(a, b, radius, landing_gap);
                if let (Some(a), Some(b)) = (&mut a.embedded_mtext, &b.embedded_mtext) {
                    points!(a, b, insertion_point, normal);
                    numbers!(a, b, height, rectangle_width);
                }
            }
            (E::CoordinationModel(a), E::CoordinationModel(b)) => {
                for (a, b) in a.transform.iter_mut().zip(b.transform) {
                    near(*a, b);
                    *a = b;
                }
            }
            (E::PointCloud(a), E::PointCloud(b)) => {
                points!(
                    a,
                    b,
                    origin,
                    extents_min,
                    extents_max,
                    ucs_origin,
                    ucs_x_direction,
                    ucs_y_direction,
                    ucs_z_direction
                );
            }
            (E::PointCloudEx(a), E::PointCloudEx(b)) => {
                points!(
                    a,
                    b,
                    ucs_origin,
                    ucs_x_direction,
                    ucs_y_direction,
                    ucs_z_direction
                );
            }
            _ => {}
        },
        _ => panic!(),
    }
}
fn numeric_pairs(path: &str) -> acadrust::Result<Vec<(i32, String)>> {
    use acadrust::io::dxf::GroupCodeValueType as K;
    use std::io::{BufRead, Cursor, Read};
    let bytes = std::fs::read(path)?;
    if !bytes.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0") {
        return crate::remove_polylines::raw_string_pairs(path);
    }
    let mut input = Cursor::new(&bytes[22..]);
    let mut pairs = vec![];
    while input.position() < input.get_ref().len() as u64 {
        let mut c = [0; 2];
        input.read_exact(&mut c)?;
        let code = u16::from_le_bytes(c) as i32;
        let value = match K::from_raw_code(code) {
            K::String | K::Handle => {
                let mut b = vec![];
                input.read_until(0, &mut b)?;
                assert_eq!(b.pop(), Some(0));
                String::from_utf8(b).unwrap()
            }
            K::Int16 | K::Byte => {
                let mut b = [0; 2];
                input.read_exact(&mut b)?;
                i16::from_le_bytes(b).to_string()
            }
            K::Int32 => {
                let mut b = [0; 4];
                input.read_exact(&mut b)?;
                i32::from_le_bytes(b).to_string()
            }
            K::Int64 => {
                let mut b = [0; 8];
                input.read_exact(&mut b)?;
                i64::from_le_bytes(b).to_string()
            }
            K::Double => {
                let mut b = [0; 8];
                input.read_exact(&mut b)?;
                f64::from_le_bytes(b).to_string()
            }
            K::Bool => {
                let mut b = [0];
                input.read_exact(&mut b)?;
                b[0].to_string()
            }
            K::BinaryData => {
                let mut n = [0];
                input.read_exact(&mut n)?;
                let mut b = vec![0; n[0] as usize];
                input.read_exact(&mut b)?;
                String::new()
            }
            _ => panic!("Unexpected code {code}"),
        };
        pairs.push((code, value));
    }
    Ok(pairs)
}
pub(crate) fn strip_format_raw_xdata(e: &mut EntityType) {
    if let EntityType::Extended(e) = e {
        if let E::Format(v) = &mut e.data {
            if let Some(codes) = &mut v.raw_dxf_codes {
                if let Some(i) = codes.iter().position(|(c, _)| *c >= 1000) {
                    codes.truncate(i);
                }
            }
        }
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let mut source = DxfReader::from_file(input)?.read()?;
    // Upstream PointReader does not classify 43/53/63 as coordinates, so
    // LIGHT readback falls back to (1,1,1). Restore authored input solely for
    // the independent numerical transform oracle; serialized comparison below
    // still uses the unmodified upstream reader and its projection.
    let pairs = numeric_pairs(input)?;
    let mut handle = Handle::NULL;
    let mut light = false;
    let mut photo = false;
    let mut y_seen = false;
    for (code, value) in pairs {
        if code == 0 {
            light = value == "LIGHT";
            photo = false;
            y_seen = false;
        }
        if !light {
            continue;
        }
        if code == 5 {
            handle = Handle::new(u64::from_str_radix(value.trim(), 16).unwrap());
        }
        if code == 295 {
            photo = true;
        }
        if photo && [43, 53, 63].contains(&code) {
            if let Some(EntityType::Light(e)) = source.get_entity_mut(handle) {
                if let Some(v) = e.photometric_data.as_mut() {
                    let x: f64 = value.trim().parse().unwrap();
                    if code == 43 {
                        v.web_rotation.x = x;
                    }
                    if code == 53 && !y_seen {
                        v.web_rotation.y = x;
                        y_seen = true;
                    }
                    if code == 63 {
                        v.web_rotation.z = x;
                    }
                }
            }
        }
    }
    for step in 0..16 {
        eprintln!("extended step {step}");
        let t = crate::entity_transform::operation(if step == 14 {
            4
        } else if step == 15 {
            6
        } else {
            step
        });
        let mut expected = source.clone();
        let rows = std::fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        assert_eq!(rows.lines().count(), source.entities().count());
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let h = Handle::new(u64::from_str_radix(p.next().unwrap(), 16).unwrap());
            let rejected = p.next().unwrap() == "1";
            let entity = expected.get_entity_mut(h).unwrap();
            let old = entity.clone();
            let mut first = old.clone();
            if entity.common().layer != "Control" {
                if step == 0 {
                    entity.translate(Vector3::new(3., -4., 5.));
                    first = entity.clone();
                } else if [5, 6, 7, 11, 14, 15].contains(&step) {
                    entity.apply_mirror(&t);
                    first = entity.clone();
                    if step == 15 {
                        entity.apply_mirror(&t);
                    }
                } else {
                    entity.apply_transform(&t);
                    first = entity.clone();
                }
            }
            let refusal = if let EntityType::Light(e) = &first {
                e.photometric_data.as_ref().is_some_and(|v| {
                    v.web_rotation.z.fract() != 0.
                        || v.web_rotation.z < -32768.
                        || v.web_rotation.z > 32767.
                })
            } else {
                false
            };
            assert_eq!(rejected, refusal, "refusal step {step} {h:?}");
            let values: Vec<f64> = p.map(|s| s.parse().unwrap()).collect();
            if let EntityType::Light(l) = &entity {
                if let Some(v) = &l.photometric_data {
                    point(
                        Vector3::new(values[12], values[13], values[14]),
                        v.web_rotation,
                    );
                }
            }
            if rejected {
                *entity = old;
            }
            let b = match &*entity {
                EntityType::Ole2Frame(e) => e.bounding_box(),
                EntityType::Light(e) => e.bounding_box(),
                EntityType::SectionSymbol(e) => e.bounding_box(),
                EntityType::ViewBorder(e) => e.bounding_box(),
                EntityType::Extended(e) => e.bounding_box(),
                _ => panic!(),
            };
            point(Vector3::new(values[0], values[1], values[2]), b.min);
            point(Vector3::new(values[3], values[4], values[5]), b.max);
        }
        let path = format!("{prefix}.{step}.rust.dxf");
        // Format's raw cache duplicates common XDATA in upstream serialization.
        // Keep one authoritative common copy, checked by the shared comparator.
        for e in expected.entities_mut() {
            strip_format_raw_xdata(e);
        }
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), source.entities().count());
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let h = Handle::new(u64::from_str_radix(p.next().unwrap(), 16).unwrap());
            p.next();
            let values: Vec<f64> = p.map(|s| s.parse().unwrap()).collect();
            let b = match expected.get_entity(h).unwrap() {
                EntityType::Ole2Frame(e) => e.bounding_box(),
                EntityType::Light(e) => e.bounding_box(),
                EntityType::SectionSymbol(e) => e.bounding_box(),
                EntityType::ViewBorder(e) => e.bounding_box(),
                EntityType::Extended(e) => e.bounding_box(),
                _ => panic!(),
            };
            point(Vector3::new(values[6], values[7], values[8]), b.min);
            point(Vector3::new(values[9], values[10], values[11]), b.max);
        }
        for e in expected.entities() {
            eprintln!("compare {:?}", e.common().handle);
            let mut a = actual.get_entity(e.common().handle).unwrap().clone();
            let mut b = e.clone();
            strip_format_raw_xdata(&mut a);
            strip_format_raw_xdata(&mut b);
            crate::entity_transform::compare(&a, &b);
        }
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.layers, source.layers);
    }
    Ok(())
}
