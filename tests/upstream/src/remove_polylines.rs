// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfWriter, EntityType};
use std::io::{BufRead, Cursor, Read};

// Polyface's high-level reader initializes child EntityCommon with defaults.
// Its stream readers are private. This test-only scanner uses the public upstream
// group-code layout to verify raw identities/owners in the AC1032 fixtures.
// Numeric payloads are skipped; callers use this only for string/handle metadata.
pub(crate) fn raw_string_pairs(path: &str) -> acadrust::Result<Vec<(i32,String)>> {
    use acadrust::io::dxf::GroupCodeValueType as Kind;
    let bytes = std::fs::read(path)?;
    let binary = bytes.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0");
    let mut pairs = Vec::new();
    if binary {
        assert_eq!(&bytes[22..24], &[0,0], "modern two-byte group codes");
        let mut input = Cursor::new(&bytes[22..]);
        while input.position() < input.get_ref().len() as u64 {
            let mut code = [0;2]; input.read_exact(&mut code)?;
            let code = u16::from_le_bytes(code) as i32;
            let mut value = String::new();
            let skip = match Kind::from_raw_code(code) {
                Kind::String | Kind::Handle => {
                    let mut text = Vec::new(); input.read_until(0, &mut text)?;
                    assert_eq!(text.pop(), Some(0));
                    value = String::from_utf8(text).unwrap(); 0
                }
                Kind::Int16 | Kind::Byte => 2,
                Kind::Int32 => 4,
                Kind::Int64 | Kind::Double => 8,
                Kind::Bool => 1,
                Kind::BinaryData => { let mut n = [0]; input.read_exact(&mut n)?; n[0] as u64 }
                other => panic!("unexpected fixture code {code}: {other:?}"),
            };
            assert!(input.position() + skip <= input.get_ref().len() as u64);
            input.set_position(input.position() + skip);
            pairs.push((code,value));
        }
    } else {
        let text = String::from_utf8(bytes).unwrap();
        let mut lines = text.lines();
        while let Some(code) = lines.next() {
            pairs.push((code.trim().parse().unwrap(), lines.next().unwrap().to_owned()));
        }
    }
    Ok(pairs)
}

pub(crate) fn raw_sequences(path: &str) -> acadrust::Result<Vec<(String,u64,u64)>> {
    let pairs = raw_string_pairs(path)?;
    let mut result = Vec::new();
    let mut current: Option<(String,u64,u64)> = None;
    let mut prefix = true;
    let mut depth = 0;
    for (code,value) in pairs {
        if code == 0 {
            if let Some(record) = current.take() { result.push(record); }
            let kind = value.as_str();
            if matches!(kind, "POLYLINE" | "VERTEX" | "SEQEND") {
                current = Some((kind.to_owned(),0,0));
            }
            prefix = true; depth = 0;
        } else if code == 102 {
            if value.starts_with('{') { depth += 1; }
            else if value == "}" && depth > 0 { depth -= 1; }
        } else if depth == 0 {
            if matches!(code, 100 | 1001) { prefix = false; }
            if prefix {
                if let Some(record) = current.as_mut() {
                    if code == 5 { record.1 = u64::from_str_radix(value.trim(),16).unwrap(); }
                    if code == 330 { record.2 = u64::from_str_radix(value.trim(),16).unwrap(); }
                }
            }
        }
    }
    if let Some(record) = current { result.push(record); }
    Ok(result)
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let actual = DxfReader::from_file(after)?.read()?;
    let binary = std::fs::read(before)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0");
    assert_eq!(std::fs::read(after)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0"), binary);
    let entities: Vec<_> = original.entities().collect();
    assert_eq!(entities.len(), 2);
    let first = entities[0];
    let second = entities[1];
    match (first, second) {
        (EntityType::Polyline2D(a), EntityType::Polyline2D(b)) => {
            assert!(a.is_closed() && !b.is_closed());
            assert_eq!(a.vertices.len(), 4);
            assert_eq!(b.vertices.len(), 3);
            assert_eq!(a.vertices[3].bulge, -0.25);
        }
        (EntityType::Polyline3D(a), EntityType::Polyline3D(b)) => {
            assert!(a.flags.closed && !b.flags.closed);
            assert_eq!(a.vertices.len(), 4);
            assert_eq!(b.vertices.len(), 3);
            assert_eq!(b.vertices.iter().map(|v| v.position.z).collect::<Vec<_>>(), vec![11.,-4.5,9.]);
        }
        (EntityType::PolygonMesh(a), EntityType::PolygonMesh(b)) => {
            assert_eq!((a.m_vertex_count, a.n_vertex_count), (3,4));
            assert_eq!((b.m_vertex_count, b.n_vertex_count), (4,3));
            assert_eq!((a.vertices.len(), b.vertices.len()), (12,12));
        }
        (EntityType::PolyfaceMesh(a), EntityType::PolyfaceMesh(b)) => {
            assert_eq!((a.vertex_count(), a.face_count()), (5,3));
            assert_eq!((b.vertex_count(), b.face_count()), (5,3));
        }
        _ => panic!("expected adjacent POLYLINE sequences of the same kind"),
    }
    let removed = first.common().handle;
    let kept = second.common().handle;
    let mut reference = original.clone();
    assert_eq!(reference.remove_entity(removed).as_ref(), Some(first));
    assert!(reference.remove_entity(removed).is_none());
    assert_eq!(actual.entities().count(), 1);
    assert!(actual.get_entity(removed).is_none());
    // Full equality covers ordered vertices, XYZ, widths, bulges, flags, layers and owners.
    assert_eq!(actual.get_entity(kept), Some(second));
    assert_eq!(actual.get_entity(kept), reference.get_entity(kept));
    if let Some(EntityType::PolyfaceMesh(mesh)) = actual.get_entity(kept) {
        // Explicitly exercise the Rust signed-index/hidden-edge interpretation.
        let indices = [[1,-2,5,0],[-1,2,3,4],[3,4,-5,5]];
        let visible_indices = [vec![1,2,5], vec![1,2,3,4], vec![3,4,5]];
        let hidden = [[false,true,false,false],[true,false,false,false],[false,false,true,false]];
        for (i, face) in mesh.faces.iter().enumerate() {
            assert_eq!([face.index1,face.index2,face.index3,face.index4], indices[i]);
            assert_eq!(face.vertex_indices(), visible_indices[i]);
            assert_eq!(face.is_triangle(), i != 1);
            assert_eq!([face.is_edge1_invisible(),face.is_edge2_invisible(),
                face.is_edge3_invisible(),face.is_edge4_invisible()], hidden[i]);
        }
    }
    let original_records = raw_sequences(before)?;
    let actual_records = raw_sequences(after)?;
    let start = original_records.iter().position(|r| r.0 == "POLYLINE" && r.1 == removed.value()).unwrap();
    let end = original_records.iter().position(|r| r.0 == "POLYLINE" && r.1 == kept.value()).unwrap();
    let mut expected_records = original_records.clone();
    expected_records.drain(start..end);
    assert_eq!(actual_records, expected_records, "raw parent/vertex/face/SEQEND identity and ownership");
    assert_eq!(actual_records[0].2, second.common().owner_handle.value());
    assert_eq!(actual_records.last().unwrap().0, "SEQEND");
    let mut identities = std::collections::HashSet::new();
    for (kind, handle, owner) in &actual_records {
        assert!(*handle != 0 && identities.insert(*handle));
        if kind != "POLYLINE" { assert_eq!(*owner, kept.value()); }
    }
    assert_eq!(actual.header, original.header);
    assert_eq!(actual.version, original.version);
    assert_eq!(actual.layers, original.layers);
    assert_eq!(actual.line_types, original.line_types);
    assert_eq!(actual.text_styles, original.text_styles);
    assert_eq!(actual.dim_styles, original.dim_styles);
    assert_eq!(actual.app_ids, original.app_ids);
    assert_eq!(actual.vports, original.vports);
    assert_eq!(actual.views, original.views);
    assert_eq!(actual.ucss, original.ucss);
    assert_eq!(actual.objects, original.objects);
    assert_eq!(actual.classes, original.classes);
    assert_eq!(actual.block_records.len(), original.block_records.len());
    for block in original.block_records.iter() {
        let mut expected = block.clone();
        expected.entity_handles.retain(|h| *h != removed);
        assert_eq!(actual.block_records.get(&block.name), Some(&expected));
        let handles = |doc: &acadrust::CadDocument| doc.entities_in_block(&block.name)
            .map(|e| e.common().handle).collect::<Vec<_>>();
        assert_eq!(handles(&actual), handles(&reference));
    }
    assert_eq!(actual.model_space_entities().map(|e| e.common().handle).collect::<Vec<_>>(), vec![kept]);
    let mut writer = DxfWriter::new(&reference);
    writer.set_binary(binary);
    let written = DxfReader::from_reader(Cursor::new(writer.write_to_vec()?))?.read()?;
    assert_eq!(actual.audit(), written.audit());
    println!("First POLYLINE removed; adjacent complete entity, ordered vertices and space membership match Rust");
    Ok(())
}
