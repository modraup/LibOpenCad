// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Line;
use acadrust::io::dwg::dwg_stream_readers::object_reader::{entities::read_line, DwgObjectReader};
use acadrust::io::dwg::{
    dwg_stream_readers::{bit_reader::DwgBitReader, handle_reader::read_handles},
    dwg_stream_writers::bit_writer::DwgBitWriter,
    DwgVersion,
};
use acadrust::{CadDocument, DwgReader, DwgWriter, DxfVersion, EntityType, Vector3};
use std::fmt::Write;

pub fn generate_lines(path: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::AC1015;
    let mut line = Line::from_coords(-5., 7., 0., -5., -2., 0.);
    line.thickness = -1.25;
    line.normal = Vector3::UNIT_Y;
    line.common.color = acadrust::Color::Index(5);
    line.common.invisible = true;
    line.common.linetype_scale = 2.5;
    let paper = doc.add_paper_space_entity(EntityType::Line(line))?;
    doc.add_entity(EntityType::Line(Line::from_coords(
        1.,
        -2.,
        3.,
        1.0000000001,
        -2.001,
        -4.,
    )))?;
    let mut block = acadrust::tables::BlockRecord::new("LineSet");
    block.handle = doc.allocate_handle();
    block.block_entity_handle = doc.allocate_handle();
    block.block_end_handle = doc.allocate_handle();
    let mut child = Line::from_coords(1., 2., -3., 7., 2., -3.);
    child.common.owner_handle = block.handle;
    child.common.linetype = "BYBLOCK".into();
    child.common.line_weight = acadrust::types::LineWeight::Value(50);
    child.common.reactors.push(paper);
    doc.block_records.add(block)?;
    doc.add_entity(EntityType::Line(child))?;
    DwgWriter::write_to_file(path, &doc)
}

pub fn generate(path: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.version = DxfVersion::AC1015;
    for i in 0..900 {
        doc.add_entity(EntityType::Line(Line::from_points(
            Vector3::new(i as f64, 2., 3.),
            Vector3::new(i as f64 + 1., 4., 5.),
        )))?;
    }
    DwgWriter::write_to_file(path, &doc)
}
pub fn verify(path: &str, trace: &str, generated: bool) -> acadrust::Result<()> {
    let data = std::fs::read(path)?;
    let mut reader = DwgReader::from_file(path)?;
    let header = reader.read_file_header()?;
    let mut out = String::new();
    for (id, name) in [
        "AcDb:Header",
        "AcDb:Classes",
        "AcDb:Handles",
        "AcDb:ObjFreeSpace",
        "AcDb:Template",
        "AcDb:AuxHeader",
    ]
    .iter()
    .enumerate()
    {
        if let Some((pos, size)) = header.section_locators.get(*name) {
            writeln!(out, "S {id} {pos} {size}").unwrap();
        }
    }
    let (pos, size) = header.section_locators["AcDb:Handles"];
    let map: std::collections::BTreeMap<_, _> =
        read_handles(&data[pos as usize..(pos + size) as usize])?
            .into_iter()
            .collect();
    let objects = DwgObjectReader::new(
        data.clone(),
        DxfVersion::AC1015,
        map.clone().into_iter().collect(),
    )?;
    let doc = DwgReader::from_file(path)?.read()?;
    for (h, offset) in map {
        let mut r = DwgBitReader::new(
            data[offset as usize..offset as usize + 4].to_vec(),
            DwgVersion::AC15,
            DxfVersion::AC1015,
        );
        let size = r.read_modular_short();
        let start = offset as usize + r.position_in_bits() as usize / 8;
        let mut r = DwgBitReader::new(
            data[start..start + size as usize].to_vec(),
            DwgVersion::AC15,
            DxfVersion::AC1015,
        );
        let typ = r.read_bit_short();
        let typ_end = r.position_in_bits();
        let split = r.read_raw_long();
        let id = r.read_handle();
        let id_end = r.position_in_bits();
        assert_eq!(id, h);
        write!(
            out,
            "O {h} {offset} {start} {size} {typ} {typ_end} {split} {id_end}"
        )
        .unwrap();
        r.set_position_in_bits(split);
        if r.remaining_bits() >= 8 {
            let first = r.read_handle_relative(h);
            write!(out, " {first} {}", r.position_in_bits()).unwrap();
        }
        out.push('\n');
        if typ == 0x13 {
            let (_, mut merged) = objects.read_record_at(offset as usize)?;
            let p = objects.read_common_entity_data(&mut merged, typ);
            let l = read_line(&mut merged, DwgVersion::AC15);
            write!(
                out,
                "L {h} {} {} {} {} {} {} {} {} {} {} {} {} {} {}",
                p.entity_mode,
                p.owner_handle,
                p.layer_handle,
                p.xdictionary_handle.unwrap_or(0),
                p.prev_entity_handle.unwrap_or(0),
                p.next_entity_handle.unwrap_or(0),
                p.color.index().unwrap(),
                p.line_weight,
                u8::from(p.invisible),
                p.linetype_flags,
                p.linetype_handle,
                p.plotstyle_flags,
                p.plotstyle_handle.unwrap_or(0),
                p.reactors.len()
            )
            .unwrap();
            for reactor in &p.reactors {
                write!(out, " {reactor}").unwrap();
            }
            for value in [
                p.linetype_scale,
                l.start.x,
                l.start.y,
                l.start.z,
                l.end.x,
                l.end.y,
                l.end.z,
                l.thickness,
                l.normal.x,
                l.normal.y,
                l.normal.z,
            ] {
                write!(out, " {}", value.to_bits()).unwrap();
            }
            out.push('\n');
            let line = match doc.get_entity(h.into()) {
                Some(EntityType::Line(line)) => line,
                _ => panic!("Missing LINE {h} in Rust document"),
            };
            assert_eq!(
                (line.start, line.end, line.normal, line.thickness),
                (l.start, l.end, l.normal, l.thickness)
            );
            assert_eq!(line.common.invisible, p.invisible);
            assert_eq!(line.common.color, p.color);
            assert_eq!(line.common.linetype_scale, p.linetype_scale);
            let owner = match p.entity_mode {
                0 => p.owner_handle,
                1 => doc
                    .block_records
                    .iter()
                    .find(|b| b.name.eq_ignore_ascii_case("*Paper_Space"))
                    .unwrap()
                    .handle
                    .value(),
                2 => doc
                    .block_records
                    .iter()
                    .find(|b| b.is_model_space())
                    .unwrap()
                    .handle
                    .value(),
                _ => unreachable!(),
            };
            assert_eq!(line.common.owner_handle.value(), owner);
        }
    }
    let mut w = DwgBitWriter::new(DwgVersion::AC15, DxfVersion::AC1015);
    w.write_bit(true);
    for x in [0, 256, 255, -1234] {
        w.write_bit_short(x);
    }
    for x in [0, 100, -123456] {
        w.write_bit_long(x);
    }
    for x in [0., 1., -3.25] {
        w.write_bit_double(x);
    }
    for x in [101, 99, 0x123456, 3, 100] {
        w.write_handle_relative(100, x);
    }
    let end = w.position_in_bits();
    write!(out, "W {end} ").unwrap();
    for b in w.into_bytes() {
        write!(out, "{b:02x}").unwrap();
    }
    out.push('\n');
    let native = std::fs::read_to_string(trace)?;
    assert_eq!(native.lines().count(), out.lines().count());
    for (i, (cpp, rust)) in native.lines().zip(out.lines()).enumerate() {
        assert_eq!(cpp, rust, "DWG trace line {}", i + 1);
    }
    if generated {
        assert_eq!(
            DwgReader::from_file(path)?
                .read()?
                .entities()
                .filter(|e| matches!(e, EntityType::Line(_)))
                .count(),
            900
        );
    }
    Ok(())
}
