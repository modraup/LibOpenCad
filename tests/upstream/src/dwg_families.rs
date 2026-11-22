// SPDX-License-Identifier: MPL-2.0
use acadrust::{
    Arc, CadDocument, Circle, DwgReader, DwgWriter, DxfVersion, EntityType, Line, Vector3,
};
use std::fmt::Write;

pub fn blank(version: &str, file: &str, inches: bool) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::from_version_string(version);
    doc.header.handle_seed = 0x10000000;
    if inches { doc.header.insertion_units = 1; }
    DwgWriter::write_to_file(file, &doc)
}
pub fn generate(version: &str, file: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::from_version_string(version);
    doc.add_entity(EntityType::Line(Line::from_coords(
        -4., 2., 3., 7., 5., -2.,
    )))?;
    let mut circle = Circle::from_center_radius(Vector3::new(3., -4., 2.), 5.25);
    circle.thickness = -1.75;
    circle.normal = Vector3::UNIT_Y;
    doc.add_entity(EntityType::Circle(circle))?;
    let arc = Arc::from_center_radius_angles(Vector3::new(9., -8., 0.), 2.5, 5.75, 0.25);
    doc.add_paper_space_entity(EntityType::Arc(arc))?;
    DwgWriter::write_to_file(file, &doc)
}
pub fn verify(file: &str, trace: &str) -> acadrust::Result<()> {
    let doc = DwgReader::from_file(file)?.read()?;
    let mut lines = Vec::new();
    for entity in doc.entities() {
        let mut row = String::new();
        let common = entity.common();
        let (kind, a, b, rest) = match entity {
            EntityType::Line(l) => (
                "LINE",
                l.start,
                l.end,
                vec![l.thickness, l.normal.x, l.normal.y, l.normal.z],
            ),
            EntityType::Circle(c) => ("CIRCLE", c.center, c.normal, vec![c.radius, c.thickness]),
            EntityType::Arc(c) => (
                "ARC",
                c.center,
                c.normal,
                vec![c.radius, c.thickness, c.start_angle, c.end_angle],
            ),
            _ => continue,
        };
        write!(
            row,
            "{kind} {} {}",
            common.handle.value(),
            common.owner_handle == doc.header.paper_space_block_handle
        )
        .unwrap();
        for n in [a.x, a.y, a.z, b.x, b.y, b.z].into_iter().chain(rest) {
            write!(row, " {}", n.to_bits()).unwrap();
        }
        lines.push(row);
    }
    lines.sort();
    let cpp = std::fs::read_to_string(trace)?;
    let mut actual: Vec<_> = cpp.lines().collect();
    actual.sort();
    assert_eq!(actual, lines);
    Ok(())
}
pub fn verify_sections(file: &str, path: &str) -> acadrust::Result<()> {
    let mut r = DwgReader::from_file(file)?;
    let header = r.read_file_header()?;
    for name in [
        "AcDb:Header",
        "AcDb:Classes",
        "AcDb:Handles",
        "AcDb:AcDbObjects",
    ] {
        let raw = r.get_section_buffer(name, &header)?;
        assert_eq!(
            raw,
            std::fs::read(format!("{path}-{}", &name[5..]))?,
            "{name}"
        );
    }
    Ok(())
}

pub fn document(op: &str, file: &str, trace: &str, reference: &str) -> acadrust::Result<()> {
    verify(file, trace)?;
    let actual = DwgReader::from_file(file)?.read()?;
    let mut base = DwgReader::from_file(reference)?.read()?;
    assert_eq!(actual.version, base.version);
    assert_eq!(actual.layers, base.layers);
    assert_eq!(actual.line_types, base.line_types);
    assert_eq!(actual.text_styles, base.text_styles);
    assert_eq!(actual.app_ids, base.app_ids);
    assert_eq!(actual.dim_styles, base.dim_styles);
    assert_eq!(actual.vports, base.vports);
    assert_eq!(actual.views, base.views);
    assert_eq!(actual.ucss, base.ucss);
    assert_eq!(actual.classes, base.classes);
    assert_eq!(actual.objects, base.objects);
    let space = |doc: &CadDocument, name: &str| {
        let mut handles: Vec<_> = doc
            .entities_in_block(name)
            .map(|e| e.common().handle.value())
            .collect();
        handles.sort();
        handles
    };
    if op == "create" {
        assert_eq!(space(&actual, "*Model_Space"), [256, 257]);
        assert_eq!(space(&actual, "*Paper_Space"), [258, 259]);
        for block in base.block_records.iter_mut() {
            let b = actual.block_records.get(&block.name).unwrap();
            block.entity_handles = b.entity_handles.clone();
        }
        for e in actual.entities() {
            assert_eq!(e.common().layer, "0");
            assert!(actual.header.handle_seed > e.common().handle.value());
        }
        assert_eq!(actual.entities().count(), 4);
    } else {
        assert_eq!(actual.header, base.header);
        assert_eq!(actual.entities().count(), 3);
        assert_eq!(space(&actual, "*Model_Space"), space(&base, "*Model_Space"));
        assert_eq!(space(&actual, "*Paper_Space"), space(&base, "*Paper_Space"));
        for e in base.entities_mut() {
            if op == "edit" {
                match e {
                    EntityType::Circle(c) => c.radius = 1.,
                    EntityType::Arc(c) => c.radius = 14.75,
                    _ => {}
                }
            }
            assert_eq!(actual.get_entity(e.common().handle).unwrap(), e);
        }
    }
    assert_eq!(actual.block_records, base.block_records);
    Ok(())
}

pub fn block_fixture(path: &str) -> acadrust::Result<()> {
    use acadrust::tables::BlockRecord;
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::AC1009;
    let mut block = BlockRecord::new("DETAIL");
    block.handle = doc.allocate_handle();
    block.block_entity_handle = doc.allocate_handle();
    block.block_end_handle = doc.allocate_handle();
    let owner = block.handle;
    doc.block_records.add(block)?;
    let mut arc = Arc::from_center_radius_angles(Vector3::new(3., 1., 0.), 2.5, 0.25, 4.);
    arc.common.owner_handle = owner;
    doc.add_entity(EntityType::Arc(arc))?;
    doc.add_entity(EntityType::Circle(Circle::from_center_radius(
        Vector3::ZERO,
        7.,
    )))?;
    doc.add_paper_space_entity(EntityType::Line(Line::from_coords(4., 0., 1., 6., -2., 3.)))?;
    DwgWriter::write_to_file(path, &doc)
}
pub fn verify_block(path: &str, base: &str) -> acadrust::Result<()> {
    let actual = DwgReader::from_file(path)?.read()?;
    let mut original = DwgReader::from_file(base)?.read()?;
    let owner = original.block_records.get("DETAIL").unwrap().handle;
    let mut found = false;
    for e in original.entities_mut() {
        if let EntityType::Arc(a) = e {
            assert_eq!(a.common.owner_handle, owner);
            a.radius = 12.25;
            found = true;
        }
        assert_eq!(actual.get_entity(e.common().handle).unwrap(), e);
    }
    assert!(found);
    assert_eq!(actual.entities().count(), 3);
    assert_eq!(actual.block_records, original.block_records);
    Ok(())
}

pub fn layer_seed_fixture(version: &str, path: &str) -> acadrust::Result<()> {
    let mut doc = shared_document(version)?;
    // The AC1021 writer allocates one additional object before emitting the header.
    doc.header.handle_seed = if doc.version == DxfVersion::AC1021 {
        254
    } else {
        255
    };
    DwgWriter::write_to_file(path, &doc)
}
pub fn shared_fixture(version: &str, path: &str) -> acadrust::Result<()> {
    DwgWriter::write_to_file(path, &shared_document(version)?)
}
fn shared_document(version: &str) -> acadrust::Result<CadDocument> {
    use acadrust::tables::BlockRecord;
    use acadrust::{Layer, Point};
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::from_version_string(version);
    let mut layer = Layer::new("DETAILS");
    layer.handle = doc.allocate_handle();
    doc.layers.add(layer)?;
    let mut block = BlockRecord::new("DETAIL");
    block.handle = doc.allocate_handle();
    block.block_entity_handle = doc.allocate_handle();
    block.block_end_handle = doc.allocate_handle();
    let owner = block.handle;
    doc.block_records.add(block)?;
    let mut arc = Arc::from_center_radius_angles(Vector3::new(3., 1., 2.), 2.5, 0.25, 4.);
    arc.common.owner_handle = owner;
    arc.common.layer = "DETAILS".into();
    doc.add_entity(EntityType::Arc(arc))?;
    doc.add_entity(EntityType::Circle(Circle::from_center_radius(
        Vector3::ZERO,
        7.,
    )))?;
    let line = EntityType::Line(Line::from_coords(4., 0., 1., 6., -2., 3.));
    if doc.version >= DxfVersion::AC1015 {
        doc.add_layout("Sheet-B")?;
        doc.add_entity_to_layout(line, "Sheet-B")?;
    } else {
        doc.add_paper_space_entity(line)?;
    }
    doc.add_entity(EntityType::Point(Point::from_coords(4., 7., 2.)))?;
    Ok(doc)
}
pub fn shared_verify(path: &str, base: &str) -> acadrust::Result<()> {
    let result = DwgReader::from_file(path)?.read()?;
    let mut source = DwgReader::from_file(base)?.read()?;
    assert_eq!(result.layers, source.layers);
    assert_eq!(result.block_records, source.block_records);
    assert_eq!(result.objects, source.objects);
    assert_eq!(result.header, source.header);
    assert_eq!(result.entities().count(), source.entities().count());
    let detail = source.block_records.get("DETAIL").unwrap().handle;
    let layer = source.layers.get("DETAILS").unwrap().handle;
    let mut seen = [0; 4];
    for e in source.entities_mut() {
        let other = result.get_entity(e.common().handle).unwrap();
        match e {
            EntityType::Line(l) => {
                seen[0] += 1;
                l.end = Vector3::new(-8., 9., 2.);
            }
            EntityType::Circle(c) => {
                seen[1] += 1;
                c.center = Vector3::new(3., -5., 0.);
                c.radius = 1.;
                c.common.layer = "DETAILS".into();
                if c.common.layer_handle.is_some() {
                    c.common.layer_handle = Some(layer);
                }
            }
            EntityType::Arc(a) => {
                seen[2] += 1;
                assert_eq!(a.common.owner_handle, detail);
                assert_eq!(a.common.layer, "DETAILS");
                a.start_angle = 0.5;
                a.center.z = 9.;
            }
            EntityType::Point(_) => seen[3] += 1,
            _ => {}
        }
        assert_eq!(other, e);
    }
    assert_eq!(seen, [1; 4]);
    Ok(())
}

pub fn verify_move(path: &str, base: &str) -> acadrust::Result<()> {
    for (step, file) in [
        (1, format!("{path}.1")),
        (2, format!("{path}.2")),
        (3, path.to_owned()),
    ] {
        check_move_step(&file, base, step)?;
    }
    Ok(())
}
fn check_move_step(path: &str, base: &str, step: usize) -> acadrust::Result<()> {
    use std::collections::BTreeSet;
    let result = DwgReader::from_file(path)?.read()?;
    let mut original = DwgReader::from_file(base)?.read()?;
    let detail = original.block_records.get("DETAIL").unwrap().handle;
    let model = original.header.model_space_block_handle;
    let sheet = original
        .entities()
        .find(|e| matches!(e, EntityType::Line(_)))
        .unwrap()
        .common()
        .owner_handle;
    assert_ne!(sheet, model);
    assert_ne!(sheet, detail);
    assert_eq!(original.layers, result.layers);
    assert_eq!(original.objects, result.objects);
    assert_eq!(result.entities().count(), original.entities().count());
    let paper = original.header.paper_space_block_handle;
    for e in original.entities_mut() {
        let other = result.get_entity(e.common().handle).unwrap();
        let owner = match e {
            EntityType::Line(_) if step >= 3 => model,
            EntityType::Circle(_) => detail,
            EntityType::Arc(_) if step >= 2 => sheet,
            _ => e.common().owner_handle,
        };
        assert_eq!(other.common().owner_handle, owner);
        e.common_mut().owner_handle = owner;
        if let Some(mode) = other.common().entity_mode {
            assert_eq!(
                mode,
                if owner == model {
                    2
                } else if owner == paper {
                    1
                } else {
                    0
                }
            );
        }
        e.common_mut().entity_mode = other.common().entity_mode;
        assert_eq!(e, other);
    }
    for b in original.block_records.iter() {
        let expected: BTreeSet<_> = original
            .entities()
            .filter(|e| e.common().owner_handle == b.handle)
            .map(|e| e.common().handle)
            .collect();
        let got: Vec<_> = result
            .entities_in_block(&b.name)
            .map(|e| e.common().handle)
            .collect();
        assert_eq!(got.len(), expected.len());
        assert_eq!(got.into_iter().collect::<BTreeSet<_>>(), expected);
        let actual = result.block_records.get(&b.name).unwrap();
        assert_eq!(b.handle, actual.handle);
        assert_eq!(b.layout, actual.layout);
    }
    Ok(())
}

fn verify_layer_table(file: &str, doc: &CadDocument) -> acadrust::Result<()> {
    use acadrust::io::dwg::dwg_stream_readers::{
        handle_reader::read_handles,
        header_reader,
        object_reader::{tables, DwgObjectReader},
    };
    use std::collections::BTreeSet;
    let mut reader = DwgReader::from_file(file)?;
    let info = reader.read_file_header()?;
    let hdr = reader.get_section_buffer("AcDb:Header", &info)?;
    let header = header_reader::read_header(&hdr, doc.version, info.acad_maintenance_version)?;
    assert_eq!(header.handle_seed, doc.header.handle_seed);
    let map = read_handles(&reader.get_section_buffer("AcDb:Handles", &info)?)?;
    let control = doc.layers.handle().value();
    let payload = if doc.version >= DxfVersion::AC1018 {
        reader.get_section_buffer("AcDb:AcDbObjects", &info)?
    } else {
        std::fs::read(file)?
    };
    let new_layer_offset = map[&doc.layers.get("测量-A").unwrap().handle.value()] as usize;
    let pos = map[&control] as usize;
    let objects = DwgObjectReader::new(payload, doc.version, map)?;
    let (layer_type, mut layer) = objects.read_record_at(new_layer_offset)?;
    assert_eq!(layer_type, 51);
    assert_eq!(
        objects
            .read_common_non_entity_data(&mut layer, layer_type)
            .owner_handle,
        control
    );
    let (kind, mut record) = objects.read_record_at(pos)?;
    assert_eq!(kind, 50);
    objects.read_common_non_entity_data(&mut record, kind);
    let table = tables::read_table_control(&mut record);
    let handles: BTreeSet<_> = doc.layers.iter().map(|l| l.handle.value()).collect();
    assert_eq!(table.entry_count as usize, handles.len());
    assert_eq!(
        table.entry_handles.into_iter().collect::<BTreeSet<_>>(),
        handles
    );
    if doc.version >= DxfVersion::AC1015 {
        let aux = reader.get_section_buffer("AcDb:AuxHeader", &info)?;
        let at = if doc.version >= DxfVersion::AC1032 {
            85
        } else {
            79
        };
        assert_eq!(
            i32::from_le_bytes(aux[at..at + 4].try_into().unwrap()),
            i32::try_from(header.handle_seed).unwrap_or(-1)
        );
    }
    Ok(())
}
pub fn verify_added_layer(file: &str, base: &str) -> acadrust::Result<()> {
    let new = DwgReader::from_file(file)?.read()?;
    let old = DwgReader::from_file(base)?.read()?;
    assert_eq!(new.version, old.version);
    assert_eq!(new.layers.len(), old.layers.len() + 2);
    for (name, color) in [("测量-A", 3), ("Review", 5)] {
        let l = new.layers.get(name).unwrap();
        assert_eq!(l.name, name);
        assert_eq!(l.color, acadrust::Color::Index(color));
        assert_eq!(l.line_type.to_ascii_lowercase(), "continuous");
        assert!(l.is_plottable);
    }
    for l in old.layers.iter() {
        let other = new.layers.get(&l.name).unwrap();
        if new.version != DxfVersion::AC1009 {
            assert_eq!(l, other);
        } else {
            assert_eq!(l.color, other.color);
        }
    }
    let first = new.layers.get("测量-A").unwrap();
    let mut expected_header = old.header.clone();
    expected_header.handle_seed = new.header.handle_seed;
    if new.version != DxfVersion::AC1009 {
        assert_eq!(new.header, expected_header);
    } else {
        let raw = std::fs::read(file)?;
        let aux = raw.len() - 170;
        let count = u16::from_le_bytes(raw[0x38..0x3a].try_into().unwrap());
        assert_eq!(count as usize, new.layers.len());
        assert_eq!(raw[aux + 62..aux + 64], raw[0x38..0x3a]);
        assert_eq!(raw[aux + 64..aux + 68], raw[0x3c..0x40]);
    }
    if new.version != DxfVersion::AC1009 {
        verify_layer_table(file, &new)?;
        let last = new.layers.get("Review").unwrap();
        assert!(first.handle.value() >= old.header.handle_seed);
        assert_eq!(last.handle.value(), first.handle.value() + 1);
        assert_eq!(new.header.handle_seed, last.handle.value() + 1);
        assert_eq!(old.objects, new.objects);
        assert_eq!(old.block_records, new.block_records);
    }
    assert_eq!(new.entities().count(), old.entities().count());
    for e in old.entities() {
        let actual = new.get_entity(e.common().handle).unwrap();
        let mut expected = e.clone();
        if matches!(e, EntityType::Circle(_)) {
            expected.common_mut().layer = "测量-A".to_owned();
            if expected.common().layer_handle.is_some() {
                expected.common_mut().layer_handle = Some(first.handle);
            }
        }
        if new.version == DxfVersion::AC1009 {
            // R12 has physical symbol slots. The reader synthesizes owner handles during import.
            let owner = old
                .block_records
                .iter()
                .find(|b| b.handle == expected.common().owner_handle)
                .unwrap();
            expected.common_mut().owner_handle = new.block_records.get(&owner.name).unwrap().handle;
        }
        assert_eq!(actual, &expected);
    }
    Ok(())
}

pub fn verify_added_linetype(file: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::io::dwg::dwg_stream_readers::{
        handle_reader::read_handles,
        object_reader::{tables, DwgObjectReader},
    };
    let old = DwgReader::from_file(base)?.read()?;
    let new = DwgReader::from_file(file)?.read()?;
    let legacy = new.version == DxfVersion::AC1009;
    assert_eq!(new.version, old.version);
    assert_eq!(new.line_types.len(), old.line_types.len() + 2);
    let dash = new.line_types.get("测量-Dash").unwrap();
    let solid = new.line_types.get("SurveySolid").unwrap();
    assert_eq!(dash.description, "测量虚线");
    assert_eq!(dash.alignment, 'A');
    assert_eq!(dash.pattern_length, 6.0);
    assert_eq!(
        dash.elements.iter().map(|e| e.length).collect::<Vec<_>>(),
        [4., -1.25, 0., -0.75]
    );
    assert!(dash.elements.iter().all(|e| e.complex.is_none()));
    assert!(solid.elements.is_empty() && solid.description.is_empty());
    assert_eq!(solid.pattern_length, 0.);
    for lt in old.line_types.iter() {
        let got = new.line_types.get(&lt.name).unwrap();
        let mut expected = lt.clone();
        if legacy {
            expected.handle = got.handle;
        }
        assert_eq!(got, &expected);
    }
    assert_eq!(old.layers.len(), new.layers.len());
    for layer in old.layers.iter() {
        let got = new.layers.get(&layer.name).unwrap();
        let mut expected = layer.clone();
        if layer.name == "0" {
            expected.line_type = dash.name.clone();
        }
        if legacy {
            expected.handle = got.handle;
        }
        assert_eq!(got, &expected);
    }
    assert_eq!(old.entities().count(), new.entities().count());
    for e in old.entities() {
        let mut expected = e.clone();
        if legacy {
            let owner = old
                .block_records
                .iter()
                .find(|b| b.handle == e.common().owner_handle)
                .unwrap();
            expected.common_mut().owner_handle = new.block_records.get(&owner.name).unwrap().handle;
        }
        assert_eq!(new.get_entity(e.common().handle).unwrap(), &expected);
    }
    if legacy {
        let input = std::fs::read(base)?;
        let output = std::fs::read(file)?;
        let u16_at = |raw: &[u8], i| u16::from_le_bytes(raw[i..i + 2].try_into().unwrap());
        let u32_at = |raw: &[u8], i| u32::from_le_bytes(raw[i..i + 4].try_into().unwrap()) as usize;
        let old_count = u16_at(&input, 0x4c);
        assert_eq!(u16_at(&output, 0x4c), old_count + 2);
        let aux = output.len() - 170;
        assert_eq!(u16_at(&output, aux + 82), old_count + 2);
        assert_eq!(u32_at(&output, aux + 84), u32_at(&output, 0x50));
        let start = u32_at(&output, 0x3c);
        let target = (0..u16_at(&output, 0x38) as usize)
            .map(|i| start + i * 41)
            .find(|&row| &output[row + 1..row + 3] == b"0\0")
            .unwrap();
        assert_eq!(u16_at(&output, target + 37), old_count);
    } else {
        assert_eq!(old.block_records, new.block_records);
        assert_eq!(old.objects, new.objects);
        let mut expected_header = old.header.clone();
        expected_header.handle_seed = solid.handle.value() + 1;
        assert_eq!(new.header, expected_header);
        assert!(dash.handle.value() >= old.header.handle_seed);
        assert_eq!(dash.handle.value() + 1, solid.handle.value());
        let mut reader = DwgReader::from_file(file)?;
        let info = reader.read_file_header()?;
        let map = read_handles(&reader.get_section_buffer("AcDb:Handles", &info)?)?;
        let payload = if new.version >= DxfVersion::AC1018 {
            reader.get_section_buffer("AcDb:AcDbObjects", &info)?
        } else {
            std::fs::read(file)?
        };
        let control = new.line_types.handle().value();
        let control_offset = map[&control] as usize;
        let layer_offset = map[&new.layers.get("0").unwrap().handle.value()] as usize;
        let offsets = [map[&dash.handle.value()], map[&solid.handle.value()]];
        let objects = DwgObjectReader::new(payload, new.version, map)?;
        let version = acadrust::io::dwg::DwgVersion::from_dxf_version(new.version)?;
        let (kind, mut layer) = objects.read_record_at(layer_offset)?;
        assert_eq!(kind, 51);
        objects.read_common_non_entity_data(&mut layer, kind);
        assert_eq!(
            tables::read_layer(&mut layer, version, new.version).linetype_handle,
            dash.handle.value()
        );
        for (offset, expected_ltype) in offsets.into_iter().zip([dash, solid]) {
            let (kind, mut record) = objects.read_record_at(offset as usize)?;
            assert_eq!(kind, 57);
            assert_eq!(
                objects
                    .read_common_non_entity_data(&mut record, kind)
                    .owner_handle,
                control
            );
            let data = tables::read_linetype(&mut record, version);
            assert_eq!(data.xref_handle, 0);
            assert!(!data.xref_dependent);
            assert_eq!(data.shape_handles, vec![0; expected_ltype.elements.len()]);
            for (s, e) in data.segments.iter().zip(&expected_ltype.elements) {
                assert_eq!(s.length, e.length);
                assert_eq!((s.shape_number, s.dwg_flags), (0, 0));
                assert_eq!(
                    (s.offset_x, s.offset_y, s.scale, s.rotation),
                    (0., 0., 1., 0.)
                );
            }
        }
        let (kind, mut record) = objects.read_record_at(control_offset)?;
        assert_eq!(kind, 56);
        objects.read_common_non_entity_data(&mut record, kind);
        let table = tables::read_table_control(&mut record);
        let mut expected: Vec<_> = new
            .line_types
            .iter()
            .filter(|lt| {
                !lt.name.eq_ignore_ascii_case("ByBlock") && !lt.name.eq_ignore_ascii_case("ByLayer")
            })
            .map(|lt| lt.handle.value())
            .collect();
        let mut actual = table.entry_handles;
        expected.sort_unstable();
        actual.sort_unstable();
        assert_eq!(actual, expected);
        for name in ["ByBlock", "ByLayer"] {
            assert_eq!(
                record.read_handle(),
                new.line_types.get(name).unwrap().handle.value()
            );
        }
    }
    Ok(())
}

pub fn verify_entity_linetype(stem: &str, base: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(base)?.read()?;
    let arc_layer = source
        .entities()
        .find_map(|e| match e {
            EntityType::Arc(a) => Some(&a.common.layer),
            _ => None,
        })
        .unwrap();
    let names = ["测量-Dash", "ByBlock", "Continuous", ""];
    for step in 0..5 {
        let result = DwgReader::from_file(format!("{stem}.{}", step + 1))?.read()?;
        assert_eq!(result.version, source.version);
        assert_eq!(result.header, source.header);
        assert_eq!(result.layers, source.layers);
        assert_eq!(result.line_types, source.line_types);
        assert_eq!(result.block_records, source.block_records);
        assert_eq!(result.objects, source.objects);
        assert_eq!(result.entities().count(), source.entities().count());
        let mut checked = [0; 3];
        for original in source.entities() {
            let mut expected = original.clone();
            let slot = match &mut expected {
                EntityType::Line(_) => Some(0),
                EntityType::Circle(c) => {
                    if step == 4 {
                        c.radius = 23.5;
                        c.common.layer = arc_layer.clone();
                        if c.common.layer_handle.is_some() {
                            c.common.layer_handle =
                                Some(result.layers.get(arc_layer).unwrap().handle);
                        }
                    }
                    Some(1)
                }
                EntityType::Arc(_) => Some(2),
                _ => None,
            };
            let actual = result.get_entity(original.common().handle).unwrap();
            if let Some(slot) = slot {
                checked[slot] += 1;
                let mode = if step == 4 && slot == 1 {
                    2
                } else {
                    (step.min(3) + slot) % 4
                };
                let name = names[mode];
                // The legacy DXF bridge retains the table's original case.
                assert!(
                    actual.common().linetype.eq_ignore_ascii_case(name),
                    "{:?}, step {}, expected {:?}, got {:?}",
                    source.version,
                    step + 1,
                    name,
                    actual.common().linetype
                );
                expected.common_mut().linetype = actual.common().linetype.clone();
                let stored = match source.version {
                    DxfVersion::AC1009 => false,
                    DxfVersion::AC1012 | DxfVersion::AC1014 => mode != 3,
                    _ => mode == 0,
                };
                expected.common_mut().linetype_handle = if stored {
                    Some(source.line_types.get(name).unwrap().handle)
                } else {
                    None
                };
            }
            assert_eq!(actual, &expected);
        }
        assert_eq!(checked, [1; 3]);
    }
    Ok(())
}

pub fn color_book_fixture(path: &str) -> acadrust::Result<()> {
    use acadrust::objects::{BookColor, ObjectType};
    use acadrust::types::{Color, Transparency};
    let mut doc = shared_document("AC1018")?;
    let handle = doc.allocate_handle();
    let mut book = BookColor::new();
    book.handle = handle;
    book.owner = doc.header.named_objects_dict_handle;
    book.book_name = "Survey".into();
    book.color_name = "Ocean".into();
    book.color = Color::from_rgb(12, 90, 180);
    doc.objects.insert(handle, ObjectType::BookColor(book));
    for e in doc.entities_mut() {
        e.common_mut().transparency = Transparency::Explicit(71);
        e.common_mut().color = Color::Index(4);
        e.common_mut().color_book_handle = Some(handle);
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_entity_color(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::types::Color;
    let source = DwgReader::from_file(base)?.read()?;
    let colors = [
        Color::ByBlock,
        Color::Index(17),
        Color::from_rgb(12, 34, 56),
        Color::Index(255),
        Color::ByLayer,
        Color::Index(128),
    ];
    if base.ends_with("book-AC1018.dwg") {
        assert!(source
            .entities()
            .all(|e| e.common().color_book_handle.is_some()));
    }
    for step in 0..if base.ends_with("book-AC1018.dwg") {
        8
    } else {
        7
    } {
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.entities().count(), source.entities().count());
        for original in source.entities() {
            let mut expected = original.clone();
            let offset = match &mut expected {
                EntityType::Line(_) => Some(0),
                EntityType::Circle(c) => {
                    if step == 6 {
                        c.radius = 13.25;
                        c.common.linetype = if source.version <= DxfVersion::AC1014 {
                            source.line_types.get("Continuous").unwrap().name.clone()
                        } else {
                            "Continuous".into()
                        };
                        if matches!(source.version, DxfVersion::AC1012 | DxfVersion::AC1014) {
                            c.common.linetype_handle =
                                Some(source.line_types.get("Continuous").unwrap().handle);
                        }
                    }
                    Some(1)
                }
                EntityType::Arc(_) => Some(2),
                _ => None,
            };
            if step == 7 {
                if offset == Some(0) {
                    expected.common_mut().color = Color::Index(4);
                    expected.common_mut().color_book_handle = None;
                    expected.common_mut().color_name = None;
                }
            } else if let Some(i) = offset {
                let mut color = colors[(step.min(5) + i) % colors.len()];
                if step == 6 && i == 1 {
                    color = Color::Index(63);
                }
                if source.version < DxfVersion::AC1018 {
                    color = Color::from_index(color.approximate_index());
                }
                // The AC1009 bridge sign-extends the stored byte before parsing group 62.
                // The C++ test independently checks the actual unsigned slot and decoded ACI.
                if source.version == DxfVersion::AC1009 {
                    if let Color::Index(n) = color {
                        color = Color::from_index(n as i8 as i16);
                    }
                }
                expected.common_mut().color_name = None;
                expected.common_mut().color = color;
                expected.common_mut().color_book_handle = None;
            }
            assert_eq!(
                actual.get_entity(original.common().handle).unwrap(),
                &expected
            );
        }
    }
    Ok(())
}

pub fn verify_entity_display(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::types::LineWeight;
    let source = DwgReader::from_file(base)?.read()?;
    let weights = [0, 53, 211, -1, -2, -3];
    for step in 0..10 {
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.version, source.version);
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), source.entities().count());
        for original in source.entities() {
            let mut expected = original.clone();
            let slot = match &mut expected {
                EntityType::Line(_) => Some(0),
                EntityType::Circle(c) => {
                    if step == 9 {
                        c.radius = 8.25;
                    }
                    Some(1)
                }
                EntityType::Arc(_) => Some(2),
                _ => None,
            };
            if let Some(i) = slot {
                if source.version >= DxfVersion::AC1012 && step >= i {
                    expected.common_mut().invisible = step < i + 6 || (step == 9 && i == 1);
                }
                if source.version >= DxfVersion::AC1015 && step >= i + 3 {
                    let v = if step == 9 && i == 1 {
                        42
                    } else if step >= i + 6 {
                        weights[i + 3]
                    } else {
                        weights[i]
                    };
                    expected.common_mut().line_weight =
                        LineWeight::from_dwg_index(LineWeight::from_value(v).to_dwg_index());
                }
            }
            assert_eq!(
                actual.get_entity(original.common().handle).unwrap(),
                &expected
            );
        }
    }
    Ok(())
}

pub fn scale_fixture(version: &str, path: &str) -> acadrust::Result<()> {
    let mut doc = shared_document(version)?;
    doc.header.linetype_scale = 2.5;
    for e in doc.entities_mut() {
        e.common_mut().linetype_scale = match e {
            EntityType::Line(_) => 0.625,
            EntityType::Circle(_) => 4.5,
            EntityType::Arc(_) => 8.25,
            _ => 2.,
        };
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_entity_scale(stem: &str, base: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(base)?.read()?;
    let modern = source.version >= DxfVersion::AC1012;
    let seeded = base.contains("scale-source-");
    if seeded {
        assert_eq!(source.header.linetype_scale, 2.5);
    }
    let targets = [f64::from_bits(1.0f64.to_bits() + 1), 0., -0.375];
    for step in 0..7 {
        let result = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(result.version, source.version);
        assert_eq!(result.header, source.header);
        assert_eq!(result.layers, source.layers);
        assert_eq!(result.line_types, source.line_types);
        assert_eq!(result.block_records, source.block_records);
        assert_eq!(result.objects, source.objects);
        assert_eq!(result.entities().count(), source.entities().count());
        for source_entity in source.entities() {
            let mut expected = source_entity.clone();
            let slot = match &mut expected {
                EntityType::Line(_) => Some(0),
                EntityType::Circle(c) => {
                    if step == 6 {
                        c.radius = 12.25;
                        if modern {
                            c.common.invisible = true;
                        }
                        if source.version >= DxfVersion::AC1015 {
                            c.common.line_weight = acadrust::types::LineWeight::from_value(53);
                        }
                    }
                    Some(1)
                }
                EntityType::Arc(_) => Some(2),
                _ => None,
            };
            if seeded {
                assert_eq!(
                    source_entity.common().linetype_scale,
                    if modern {
                        slot.map(|i| [0.625, 4.5, 8.25][i]).unwrap_or(2.)
                    } else {
                        1.
                    }
                );
            }
            if let Some(i) = slot {
                if modern && step >= i {
                    expected.common_mut().linetype_scale = if step == 6 && i == 1 {
                        3.75
                    } else if step >= i + 3 {
                        1.
                    } else {
                        targets[i]
                    };
                }
            }
            let actual = result.get_entity(expected.common().handle).unwrap();
            assert_eq!(
                actual.common().linetype_scale.to_bits(),
                expected.common().linetype_scale.to_bits()
            );
            assert_eq!(actual, &expected);
        }
    }
    Ok(())
}

pub fn transparency_fixture(version: &str, path: &str) -> acadrust::Result<()> {
    use acadrust::types::{Color, Transparency};
    let mut doc = shared_document(version)?;
    for e in doc.entities_mut() {
        e.common_mut().color = Color::from_rgb(17, 89, 201);
        e.common_mut().transparency = match e {
            EntityType::Line(_) => Transparency::Explicit(42),
            EntityType::Circle(_) => Transparency::ByBlock,
            EntityType::Arc(_) => Transparency::Explicit(255),
            _ => Transparency::Explicit(0),
        };
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_entity_transparency(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::types::{Color, Transparency};
    let source = DwgReader::from_file(base)?.read()?;
    let modern = source.version >= DxfVersion::AC1018;
    let values = [
        Transparency::ByBlock,
        Transparency::Explicit(0),
        Transparency::Explicit(255),
        Transparency::Explicit(71),
        Transparency::ByLayer,
    ];
    if base.ends_with("book-AC1018.dwg") {
        assert!(source
            .entities()
            .all(|e| e.common().color_book_handle.is_some()));
    }
    for step in 0..16 {
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), source.entities().count());
        for e in source.entities() {
            let mut expected = e.clone();
            let slot = match &mut expected {
                EntityType::Line(_) => Some(0),
                EntityType::Circle(c) => {
                    if step == 15 {
                        c.radius = 9.75;
                        c.common.color = Color::Index(3);
                        c.common.color_name = None;
                        c.common.color_book_handle = None;
                        if source.version >= DxfVersion::AC1012 {
                            c.common.linetype_scale = 2.25;
                        }
                    }
                    Some(1)
                }
                EntityType::Arc(_) => Some(2),
                _ => None,
            };
            if let Some(slot) = slot {
                if modern && step >= slot {
                    let value = if step == 15 && slot == 1 {
                        Transparency::Explicit(128)
                    } else {
                        values[(step.min(14) - slot) / 3]
                    };
                    expected.common_mut().transparency = value;
                }
            }
            assert_eq!(actual.get_entity(e.common().handle).unwrap(), &expected);
        }
    }
    Ok(())
}

pub fn text_fixture(version: &str, page: &str, path: &str) -> acadrust::Result<()> {
    use acadrust::entities::text::{TextHorizontalAlignment, TextVerticalAlignment};
    use acadrust::{Text, TextStyle};
    let mut doc = shared_document(version)?;
    let mut style = TextStyle::new("NOTES");
    style.handle = doc.allocate_handle();
    doc.text_styles.add(style)?;
    let mut chinese = TextStyle::new("中文样式");
    chinese.handle = doc.allocate_handle();
    doc.text_styles.add(chinese)?;
    doc.header.code_page = page.into();
    let mut text = Text::with_value("原始中文 A", Vector3::new(3., -4., 2.));
    text.common.owner_handle = doc.block_records.get("DETAIL").unwrap().handle;
    text.common.layer = "DETAILS".into();
    text.alignment_point = Some(Vector3::new(3., 8., 2.));
    text.style = "NOTES".into();
    text.height = 2.5;
    text.rotation = 0.25;
    text.oblique_angle = -0.125;
    text.width_factor = 0.75;
    text.thickness = 1.25;
    text.normal = Vector3::UNIT_Y;
    text.generation_flags = 2;
    text.horizontal_alignment = TextHorizontalAlignment::Center;
    text.vertical_alignment = TextVerticalAlignment::Top;
    doc.add_entity(EntityType::Text(text))?;
    doc.add_entity(EntityType::Text(Text::with_value(
        "相邻中文 B",
        Vector3::new(5., 6., 0.),
    )))?;
    doc.add_paper_space_entity(EntityType::Text(Text::with_value(
        "图纸中文 C",
        Vector3::new(-7., 9., 0.),
    )))?;
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_text(stem: &str, base: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(base)?.read()?;
    for step in 0..6 {
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.text_styles, source.text_styles);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), source.entities().count());
        let mut checked = 0;
        for original in source.entities() {
            let mut expected = original.clone();
            if let EntityType::Text(t) = &mut expected {
                let target = t.value == "原始中文 A";
                if target || (step >= 4 && t.value == "图纸中文 C") {
                    t.value = match step {
                        0 => "修改中文 Ω 😀 ".repeat(400),
                        1 => String::new(),
                        2 => "短文字".into(),
                        _ => "最终中文 😀".into(),
                    };
                    t.insertion_point = if step == 5 && target {
                        Vector3::new(25., -32., 0.)
                    } else {
                        match step {
                            0 => Vector3::new(-11., 12., -3.),
                            1 => Vector3::new(7., 8., 0.),
                            _ => Vector3::new(1., -2., 4.),
                        }
                    };
                    if let Some(a) = &mut t.alignment_point {
                        a.z = t.insertion_point.z;
                    }
                    checked += 1;
                }
            }
            let found = actual.get_entity(original.common().handle).unwrap();
            assert_eq!(
                found,
                &expected,
                "TEXT step {step}, handle {:?}",
                original.common().handle
            );
        }
        assert_eq!(checked, if step >= 4 { 2 } else { 1 });
    }
    Ok(())
}

pub fn text_style_boundary(version: &str, path: &str) -> acadrust::Result<()> {
    text_fixture(version, "ANSI_936", path)?;
    let mut doc = DwgReader::from_file(path)?.read()?;
    for entity in doc.entities_mut() {
        if let EntityType::Text(t) = entity {
            if t.value == "原始中文 A" {
                t.oblique_angle = 0.148847420517342; // degree/radian projection changes the last bit
            }
        }
    }
    for i in 3..=256 {
        let mut style = acadrust::TextStyle::new(if i == 255 {
            "SLOT255".into()
        } else if i == 256 {
            "SLOT256".into()
        } else {
            format!("STYLE{i}")
        });
        style.handle = doc.allocate_handle();
        doc.text_styles.add(style)?;
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_text_format(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::entities::text::{TextHorizontalAlignment as H, TextVerticalAlignment as V};
    let source = DwgReader::from_file(base)?.read()?;
    let legacy = source.version == DxfVersion::AC1009;
    let boundary = base.contains("boundary");
    let mut expected: Vec<_> = source.entities().cloned().collect();
    let target = expected
        .iter()
        .position(|e| matches!(e, EntityType::Text(t) if t.value=="原始中文 A"))
        .unwrap();
    let paper = expected
        .iter()
        .position(|e| matches!(e, EntityType::Text(t) if t.value=="图纸中文 C"))
        .unwrap();
    for step in 0..10 {
        let EntityType::Text(t) = &mut expected[if step == 7 { paper } else { target }] else {
            unreachable!()
        };
        match step {
            0 => t.height = 6.25,
            1 => {
                t.rotation = -0.375;
                t.width_factor = 1.75;
            }
            2 => {
                t.horizontal_alignment = H::Fit;
                t.vertical_alignment = V::Middle;
                t.alignment_point = Some(Vector3::new(15., -9., 2.));
                t.style = "中文样式".into();
            }
            3 => {
                t.height = 1.;
                t.rotation = 0.;
                t.width_factor = 1.;
                t.horizontal_alignment = H::Left;
                t.vertical_alignment = V::Baseline;
                t.alignment_point = None;
                t.style = source
                    .text_styles
                    .iter()
                    .find(|s| s.name.eq_ignore_ascii_case("standard"))
                    .unwrap()
                    .name
                    .clone();
            }
            4 => {
                t.height = 2.75;
                t.rotation = std::f64::consts::PI / 7.;
                t.width_factor = 0.625;
                t.horizontal_alignment = H::Aligned;
                t.vertical_alignment = V::Bottom;
                t.alignment_point = Some(Vector3::new(30., 40., 2.));
            }
            5 => {
                t.style = "NOTES".into();
                t.horizontal_alignment = H::Middle;
                t.vertical_alignment = V::Top;
                t.alignment_point = Some(Vector3::new(3., -4., 2.));
            }
            6 => {
                t.value = "格式与中文 😀".into();
                t.insertion_point = Vector3::new(10., 20., 4.);
                t.height = 9.5;
                t.rotation = 0.5;
                t.width_factor = 2.;
                t.horizontal_alignment = H::Right;
                t.vertical_alignment = V::Baseline;
                t.alignment_point = Some(Vector3::new(-8., 12., 4.));
                t.style = "中文样式".into();
            }
            7 => {
                t.height = 3.;
                t.rotation = 0.125;
                t.width_factor = 1.5;
                t.horizontal_alignment = H::Center;
                t.vertical_alignment = V::Bottom;
                t.alignment_point = Some(Vector3::new(9., 11., 0.));
                t.style = "NOTES".into();
            }
            8 => {
                t.style = if boundary {
                    "SLOT255".into()
                } else {
                    source
                        .text_styles
                        .iter()
                        .find(|s| s.name.eq_ignore_ascii_case("standard"))
                        .unwrap()
                        .name
                        .clone()
                }
            }
            9 if boundary && legacy => {}
            9 => t.style = if boundary { "SLOT256" } else { "NOTES" }.into(),
            _ => unreachable!(),
        }
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.text_styles, source.text_styles);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), expected.len());
        for wanted in &expected {
            let mut found = actual.get_entity(wanted.common().handle).unwrap().clone();
            if let (EntityType::Text(a), EntityType::Text(b)) = (&mut found, wanted) {
                assert!(
                    (a.rotation - b.rotation).abs() < 1e-14,
                    "rotation at step {step}"
                );
                a.rotation = b.rotation;
            }
            assert_eq!(&found, wanted, "TEXT format step {step}");
        }
    }
    Ok(())
}

pub fn verify_text_geometry(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::entities::text::{TextHorizontalAlignment as H, TextVerticalAlignment as V};
    let source = DwgReader::from_file(base)?.read()?;
    let legacy = source.version == DxfVersion::AC1009;
    let mut expected: Vec<_> = source.entities().cloned().collect();
    let target = expected
        .iter()
        .position(|e| matches!(e,EntityType::Text(t) if t.value=="原始中文 A"))
        .unwrap();
    let paper = expected
        .iter()
        .position(|e| matches!(e,EntityType::Text(t) if t.value=="图纸中文 C"))
        .unwrap();
    for step in 0..12 {
        let EntityType::Text(t) = &mut expected[if step == 8 || step == 9 {
            paper
        } else {
            target
        }] else {
            unreachable!()
        };
        match step {
            0 => {
                t.oblique_angle = 0.375;
                t.generation_flags = 4;
                t.thickness = -2.25;
                t.normal = Vector3::new(1., 2., 3.);
            }
            1 | 7 | 9 => {
                t.oblique_angle = 0.;
                t.generation_flags = 0;
                t.thickness = 0.;
                t.normal = Vector3::UNIT_Z;
            }
            2 => {
                t.oblique_angle = -std::f64::consts::PI / 9.;
                t.generation_flags = 2;
                t.thickness = 0.75;
                t.normal = Vector3::UNIT_X;
            }
            3 => {
                t.oblique_angle = 0.148847420517342;
                t.generation_flags = 6;
                t.thickness = 0.;
                t.normal = Vector3::new(0., 0., -1.);
            }
            4 => {
                t.oblique_angle = 0.;
                t.generation_flags = 255;
                t.thickness = -1.5;
                t.normal = Vector3::ZERO;
            }
            5 => {
                t.oblique_angle = -0.25;
                t.generation_flags = 262;
                t.thickness = 1.25;
                t.normal = Vector3::new(0.25, -0.5, 2.);
            }
            6 => t.generation_flags = -1,
            8 => {
                t.oblique_angle = -0.125;
                t.generation_flags = 6;
                t.thickness = 2.5;
                t.normal = Vector3::new(-2., 3., 4.);
            }
            10 => {
                t.oblique_angle = 0.148847420517342;
                t.generation_flags = 6;
                t.thickness = -0.625;
                t.normal = Vector3::new(1., -2., 3.);
                t.value = "倾斜镜像 中文 😀".into();
                t.insertion_point = Vector3::new(10., -12., 5.);
                t.alignment_point = Some(Vector3::new(-2., 9., 5.));
                t.style = "中文样式".into();
                t.height = 6.;
                t.rotation = -0.5;
                t.width_factor = 0.75;
                t.horizontal_alignment = H::Right;
                t.vertical_alignment = V::Bottom;
            }
            11 => {
                t.height = 8.;
                t.width_factor = 1.25;
            }
            _ => unreachable!(),
        }
        if legacy {
            t.generation_flags = t.generation_flags as u8 as i16;
        }
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.text_styles, source.text_styles);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), expected.len());
        for wanted in &expected {
            let mut found = actual.get_entity(wanted.common().handle).unwrap().clone();
            if let (EntityType::Text(a), EntityType::Text(b)) = (&mut found, wanted) {
                assert!(
                    (a.oblique_angle - b.oblique_angle).abs() < 1e-14,
                    "oblique at step {step}"
                );
                a.oblique_angle = b.oblique_angle;
            }
            assert_eq!(&found, wanted, "TEXT geometry step {step}");
        }
    }
    Ok(())
}

pub fn text_common_fixture(
    version: &str,
    page: &str,
    book: bool,
    path: &str,
) -> acadrust::Result<()> {
    use acadrust::objects::{BookColor, ObjectType};
    use acadrust::tables::LineTypeElement;
    use acadrust::types::{Color, LineWeight, Transparency};
    use acadrust::{Layer, LineType};
    text_fixture(version, page, path)?;
    let mut doc = DwgReader::from_file(path)?.read()?;
    let mut layer = Layer::new("测量");
    layer.handle = doc.allocate_handle();
    doc.layers.add(layer)?;
    let mut lt = LineType::new("中文线型");
    lt.handle = doc.allocate_handle();
    lt.elements = vec![LineTypeElement::dash(2.), LineTypeElement::space(0.5)];
    lt.pattern_length = 2.5;
    doc.line_types.add(lt)?;
    let book_handle = if book {
        let h = doc.allocate_handle();
        let mut b = BookColor::new();
        b.handle = h;
        b.owner = doc.header.named_objects_dict_handle;
        b.book_name = "Survey".into();
        b.color_name = "Ocean".into();
        b.color = Color::from_rgb(12, 90, 180);
        doc.objects.insert(h, ObjectType::BookColor(b));
        Some(h)
    } else {
        None
    };
    let modern = doc.version >= DxfVersion::AC1018;
    for e in doc.entities_mut() {
        if let EntityType::Text(t) = e {
            t.common.color = if modern && !book {
                Color::from_rgb(90, 100, 110)
            } else {
                Color::Index(4)
            };
            t.common.color_book_handle = book_handle;
            t.common.transparency = Transparency::Explicit(31);
            t.common.line_weight = LineWeight::from_value(30);
            t.common.linetype_scale = 0.625;
            t.common.linetype = "中文线型".into();
        }
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_text_common(stem: &str, base: &str) -> acadrust::Result<()> {
    use acadrust::types::{Color, LineWeight, Transparency};
    let source = DwgReader::from_file(base)?.read()?;
    let v = source.version;
    let legacy = v == DxfVersion::AC1009;
    let mut expected: Vec<_> = source.entities().cloned().collect();
    let target = expected
        .iter()
        .position(|e| matches!(e,EntityType::Text(t) if t.value=="原始中文 A"))
        .unwrap();
    let paper = expected
        .iter()
        .position(|e| matches!(e,EntityType::Text(t) if t.value=="图纸中文 C"))
        .unwrap();
    for step in 0..10 {
        let EntityType::Text(t) = &mut expected[if step == 8 { paper } else { target }] else {
            unreachable!()
        };
        let mut lt = None;
        let mut color = None;
        let mut alpha = None;
        let mut weight = None;
        let mut scale = None;
        let mut visible = None;
        match step {
            0 => alpha = Some(Transparency::ByBlock),
            1 => {
                t.common.layer = "测量".into();
                lt = Some("中文线型");
                visible = Some(true);
                weight = Some(53);
                scale = Some(2.25);
            }
            2 => {
                if base.contains("book") {
                    color = Some(Color::Index(4));
                }
            }
            3 => {
                color = Some(Color::ByBlock);
                lt = Some("ByBlock");
                alpha = Some(Transparency::Explicit(0));
            }
            4 => {
                color = Some(Color::Index(17));
                lt = Some("Continuous");
                alpha = Some(Transparency::Explicit(255));
                weight = Some(-2);
                scale = Some(0.);
            }
            5 => {
                color = Some(Color::from_rgb(12, 34, 56));
                lt = Some("ByLayer");
                alpha = Some(Transparency::Explicit(71));
                weight = Some(-3);
                scale = Some(-0.375);
            }
            6 => {
                color = Some(Color::Index(255));
                lt = Some("中文线型");
                t.common.layer = "0".into();
                visible = Some(false);
                weight = Some(211);
            }
            7 => {
                color = Some(Color::ByLayer);
                lt = Some("ByLayer");
                alpha = Some(Transparency::ByLayer);
                weight = Some(-1);
                scale = Some(1.);
            }
            8 | 9 => {
                t.common.layer = if step == 8 { "测量" } else { "DETAILS" }.into();
                lt = Some(if step == 8 { "中文线型" } else { "ByBlock" });
                color = Some(if step == 8 {
                    Color::from_rgb(90, 120, 150)
                } else {
                    Color::Index(2)
                });
                visible = Some(true);
                weight = Some(if step == 8 { 25 } else { 50 });
                scale = Some(if step == 8 { 3.5 } else { 0.625 });
                alpha = Some(if step == 8 {
                    Transparency::Explicit(128)
                } else {
                    Transparency::ByBlock
                });
                t.value = if step == 8 {
                    "图纸属性 中文 😀"
                } else {
                    "组合属性 中文 😀"
                }
                .into();
                t.insertion_point = if step == 8 {
                    Vector3::new(11., 22., 3.)
                } else {
                    Vector3::new(3., 4., 5.)
                };
                if let Some(a) = &mut t.alignment_point {
                    a.z = t.insertion_point.z;
                }
                t.height = 9.;
                t.thickness = -0.75;
                t.normal = Vector3::UNIT_X;
            }
            _ => unreachable!(),
        }
        if !legacy {
            t.common.layer_handle = Some(source.layers.get(&t.common.layer).unwrap().handle);
        }
        if let Some(name) = lt {
            let table_ref = if legacy {
                name == "Continuous" || name == "中文线型"
            } else if v <= DxfVersion::AC1014 {
                name != "ByLayer"
            } else {
                name == "中文线型"
            };
            t.common.linetype = if name == "ByLayer" {
                String::new()
            } else if table_ref {
                source.line_types.get(name).unwrap().name.clone()
            } else if legacy && name == "ByBlock" {
                "BYBLOCK".into()
            } else {
                name.into()
            };
            t.common.linetype_handle = if table_ref && !legacy {
                Some(source.line_types.get(name).unwrap().handle)
            } else {
                None
            };
        }
        if let Some(mut c) = color {
            if v < DxfVersion::AC1018 {
                c = Color::from_index(c.approximate_index());
            }
            if legacy {
                if let Color::Index(n) = c {
                    c = Color::from_index(n as i8 as i16);
                }
            }
            t.common.color = c;
            t.common.color_name = None;
            t.common.color_book_handle = None;
        }
        if v >= DxfVersion::AC1012 {
            if let Some(s) = scale {
                t.common.linetype_scale = s;
            }
            if let Some(b) = visible {
                t.common.invisible = b;
            }
        }
        if v >= DxfVersion::AC1015 {
            if let Some(w) = weight {
                t.common.line_weight = LineWeight::from_value(w);
            }
        }
        if v >= DxfVersion::AC1018 {
            if let Some(a) = alpha {
                t.common.transparency = a;
            }
        }
        let actual = DwgReader::from_file(format!("{stem}.{step}"))?.read()?;
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.line_types, source.line_types);
        assert_eq!(actual.text_styles, source.text_styles);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
        assert_eq!(actual.entities().count(), expected.len());
        for wanted in &expected {
            assert_eq!(
                actual.get_entity(wanted.common().handle).unwrap(),
                wanted,
                "TEXT common step {step}"
            );
        }
    }
    Ok(())
}

pub fn verify_text_create(path: &str) -> acadrust::Result<()> {
    let original = DwgReader::from_file(path)?.read()?;
    assert_eq!(original.entities().count(), 6);
    for (id, value, position, height, paper) in [
        (
            0x100,
            "新建中文 A Ω".to_string(),
            Vector3::new(3., -4., 2.),
            2.5,
            false,
        ),
        (
            0x102,
            "Paper B".to_string(),
            Vector3::new(-7., 9., 0.),
            4.,
            true,
        ),
        (0x104, "".to_string(), Vector3::ZERO, 1., false),
        (0x105, "x".repeat(2200) + "中文😀", Vector3::ZERO, 1., false),
    ] {
        let e = original.get_entity(acadrust::Handle::new(id)).unwrap();
        let EntityType::Text(t) = e else {
            panic!("Missing created TEXT")
        };
        assert_eq!(t.value, value);
        assert_eq!(t.insertion_point, position);
        assert_eq!(t.height, height);
        assert_eq!(t.style.to_uppercase(), "STANDARD");
        assert_eq!(
            t.normal,
            if id == 0x100 {
                Vector3::UNIT_Y
            } else {
                Vector3::UNIT_Z
            }
        );
        assert_eq!(t.thickness, if id == 0x100 { -1.25 } else { 0. });
        assert_eq!(
            t.common.owner_handle,
            if paper {
                original.header.paper_space_block_handle
            } else {
                original.header.model_space_block_handle
            }
        );
    }
    let edited = DwgReader::from_file(format!("{path}.edited"))?.read()?;
    for e in original.entities() {
        let mut expected = e.clone();
        if e.common().handle.value() == 0x100 {
            let EntityType::Text(t) = &mut expected else {
                unreachable!()
            };
            t.value = "创建后修改".into();
            t.common.owner_handle = edited.header.paper_space_block_handle;
            t.common.entity_mode = edited
                .get_entity(t.common.handle)
                .unwrap()
                .common()
                .entity_mode;
        }
        assert_eq!(&expected, edited.get_entity(e.common().handle).unwrap());
    }
    Ok(())
}
pub fn verify_text_move(path: &str, base: &str) -> acadrust::Result<()> {
    use std::collections::BTreeSet;
    let source = DwgReader::from_file(base)?.read()?;
    let model = source.header.model_space_block_handle;
    let paper = source.header.paper_space_block_handle;
    let detail = source.block_records.get("DETAIL").unwrap().handle;
    let sheet = source
        .entities()
        .find(|e| matches!(e, EntityType::Line(_)))
        .unwrap()
        .common()
        .owner_handle;
    let mut expected: Vec<_> = source.entities().cloned().collect();
    for step in 0..6 {
        let result = DwgReader::from_file(format!("{path}.{step}"))?.read()?;
        let owner = match step {
            0 | 5 => model,
            1 => sheet,
            2 => paper,
            _ => detail,
        };
        let name = if step == 4 {
            "图纸中文 C"
        } else {
            "原始中文 A"
        };
        for e in &mut expected {
            if matches!(e,EntityType::Text(t) if t.value==name) {
                e.common_mut().owner_handle = owner;
                e.common_mut().entity_mode = result
                    .get_entity(e.common().handle)
                    .unwrap()
                    .common()
                    .entity_mode;
                if let Some(mode) = e.common().entity_mode {
                    assert_eq!(
                        mode,
                        if owner == model {
                            2
                        } else if owner == paper {
                            1
                        } else {
                            0
                        }
                    );
                }
            }
            assert_eq!(&*e, result.get_entity(e.common().handle).unwrap());
        }
        assert_eq!(result.entities().count(), expected.len());
        assert_eq!(source.header, result.header);
        assert_eq!(source.layers, result.layers);
        assert_eq!(source.line_types, result.line_types);
        assert_eq!(source.text_styles, result.text_styles);
        assert_eq!(source.objects, result.objects);
        for b in source.block_records.iter() {
            let want: BTreeSet<_> = expected
                .iter()
                .filter(|e| e.common().owner_handle == b.handle)
                .map(|e| e.common().handle)
                .collect();
            let got: Vec<_> = result
                .entities_in_block(&b.name)
                .map(|e| e.common().handle)
                .collect();
            assert_eq!(got.len(), want.len());
            assert_eq!(got.into_iter().collect::<BTreeSet<_>>(), want);
            let actual = result.block_records.get(&b.name).unwrap();
            assert_eq!(actual.handle, b.handle);
            assert_eq!(actual.layout, b.layout);
        }
    }
    Ok(())
}

pub fn compound_fixture(version: &str, path: &str) -> acadrust::Result<()> {
    use acadrust::entities::{
        AttributeDefinition, AttributeEntity, Insert, PolyfaceFace, PolyfaceMesh,
        PolygonMeshEntity, PolygonMeshFlags, PolygonMeshVertex, Polyline2D, Polyline3D, Vertex2D,
    };
    use acadrust::tables::BlockRecord;
    let mut doc = shared_document(version)?;
    let mut symbol = BlockRecord::new("SYMBOL");
    symbol.handle = doc.allocate_handle();
    symbol.block_entity_handle = doc.allocate_handle();
    symbol.block_end_handle = doc.allocate_handle();
    doc.block_records.add(symbol)?;
    for offset in [0., 20.] {
        let mut p = Polyline2D::new();
        p.close();
        p.start_width = 0.25;
        p.end_width = 0.5;
        for (i, (x, y)) in [(0., 0.), (8., 0.), (8., 6.), (0., 6.)]
            .into_iter()
            .enumerate()
        {
            let mut v = Vertex2D::new(Vector3::new(x + offset, y, 0.));
            v.start_width = (i + 1) as f64 * 0.25;
            v.end_width = (i + 1) as f64 * 0.5;
            v.bulge = if i == 3 { -0.5 } else { 0. };
            p.add_vertex(v);
        }
        doc.add_entity(EntityType::Polyline2D(p))?;
        let mut p = Polyline3D::new();
        p.close();
        for (x, y, z) in [(0., 0., -2.), (8., 0., 3.), (8., 6., 7.5)] {
            p.add_vertex(Vector3::new(x + offset, y, z));
        }
        doc.add_entity(EntityType::Polyline3D(p))?;
        let mut m = PolygonMeshEntity::new();
        m.m_vertex_count = 2;
        m.n_vertex_count = 3;
        m.flags.insert(PolygonMeshFlags::CLOSED_N);
        for i in 0..2 {
            for j in 0..3 {
                m.vertices.push(PolygonMeshVertex::at(Vector3::new(
                    offset + i as f64,
                    j as f64,
                    (i * 3 - j) as f64,
                )));
            }
        }
        doc.add_entity(EntityType::PolygonMesh(m))?;
        let mut m = PolyfaceMesh::new();
        for (x, y, z) in [
            (0., 0., 0.),
            (4., 0., 1.),
            (4., 4., 2.),
            (0., 4., 3.),
            (2., 2., 5.),
        ] {
            m.add_vertex_xyz(offset + x, y, z);
        }
        m.add_face(PolyfaceFace::triangle(1, -2, 5));
        m.add_face(PolyfaceFace::quad(-1, 2, 3, 4));
        doc.add_entity(EntityType::PolyfaceMesh(m))?;
        let mut i = Insert::new("SYMBOL", Vector3::new(offset, 2., 3.));
        for tag in ["ONE", "TWO"] {
            let a = AttributeDefinition::new(tag.into(), "Prompt".into(), "中文值".into());
            i.attributes
                .push(AttributeEntity::from_definition(&a, None));
        }
        doc.add_entity(EntityType::Insert(i.clone()))?;
        i.column_count = 2;
        i.row_count = 3;
        i.column_spacing = 5.;
        i.row_spacing = 7.;
        doc.add_entity(EntityType::Insert(i))?;
    }
    DwgWriter::write_to_file(path, &doc)
}

pub fn verify_compound_move(path: &str, base: &str) -> acadrust::Result<()> {
    use std::collections::BTreeSet;
    let source = DwgReader::from_file(base)?.read()?;
    let target: BTreeSet<_> = source
        .entities()
        .filter(|e| {
            matches!(
                e,
                EntityType::Polyline2D(_)
                    | EntityType::Polyline3D(_)
                    | EntityType::PolygonMesh(_)
                    | EntityType::PolyfaceMesh(_)
                    | EntityType::Insert(_)
            )
        })
        .map(|e| e.common().handle)
        .collect::<BTreeSet<_>>()
        .into_iter()
        .take(6)
        .collect();
    assert_eq!(target.len(), 6);
    let model = source.header.model_space_block_handle;
    let paper = source.header.paper_space_block_handle;
    let detail = source.block_records.get("DETAIL").unwrap().handle;
    let sheet = source
        .entities()
        .find(|e| matches!(e, EntityType::Line(_)))
        .unwrap()
        .common()
        .owner_handle;
    for step in 0..4 {
        let actual = DwgReader::from_file(format!("{path}.{step}"))?.read()?;
        let owner = match step {
            0 => detail,
            1 => sheet,
            2 => paper,
            _ => model,
        };
        assert_eq!(actual.entities().count(), source.entities().count());
        for e in source.entities() {
            let mut expected = e.clone();
            if target.contains(&e.common().handle) {
                expected.common_mut().owner_handle = owner;
                let mode = actual
                    .get_entity(e.common().handle)
                    .unwrap()
                    .common()
                    .entity_mode;
                if let Some(m) = mode {
                    assert_eq!(
                        m,
                        if owner == model {
                            2
                        } else if owner == paper {
                            1
                        } else {
                            0
                        }
                    );
                }
                expected.common_mut().entity_mode = mode;
                if source.version == DxfVersion::AC1009 {
                    if let EntityType::Insert(insert) = &mut expected {
                        for attribute in &mut insert.attributes {
                            attribute.common.entity_mode =
                                if owner == paper { Some(1) } else { None };
                        }
                    }
                }
            }
            assert_eq!(
                &expected,
                actual.get_entity(e.common().handle).unwrap(),
                "step {step} handle {}",
                e.common().handle.value()
            );
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.line_types, actual.line_types);
        assert_eq!(source.objects, actual.objects);
        for b in source.block_records.iter() {
            let want: BTreeSet<_> = source
                .entities()
                .filter(|e| {
                    if target.contains(&e.common().handle) {
                        owner == b.handle
                    } else {
                        e.common().owner_handle == b.handle
                    }
                })
                .map(|e| e.common().handle)
                .collect();
            let got: Vec<_> = actual
                .entities_in_block(&b.name)
                .map(|e| e.common().handle)
                .collect();
            assert_eq!(got.len(), want.len());
            assert_eq!(got.into_iter().collect::<BTreeSet<_>>(), want);
            assert_eq!(actual.block_records.get(&b.name).unwrap().layout, b.layout);
        }
    }
    Ok(())
}
