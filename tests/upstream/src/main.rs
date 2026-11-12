// SPDX-License-Identifier: MPL-2.0
mod symbol_operations;
mod review;
mod reference_operations;
mod encoding_conversion;
mod stage2;
mod curves;
mod colors;
mod entity_properties;
mod header;
mod handles;
mod spaces;
mod index;
mod ownership;
mod symbols;
mod add_symbols;
mod defaults;
mod add_entities;
mod remove_entities;
mod remove_polylines;
mod reactor_cleanup;
mod extension_delete;
mod copy_entities;
mod copy_polylines;
mod remove_extended_entities;
mod lwpolyline;
mod polyline2d;
mod polyline3d;
mod polygon_mesh;
mod polyface_mesh;
mod transforms;
mod vectors;

use acadrust::{
    CadDocument, Circle, Color, DxfReader, DxfVersion, DxfWriter, EntityType, Layer, Line, Text,
    Vector3,
};

fn generate(path: &str, binary: bool) -> acadrust::Result<()> {
    let (geometry, notes, label) = if binary {
        ("几何", "标注", "一层平面图")
    } else {
        ("Geometry", "Notes", "Revision A")
    };
    let mut doc = CadDocument::new();
    doc.layers
        .add(Layer::with_color(geometry, Color::Index(3)))?;
    doc.layers.add(Layer::with_color(notes, Color::Index(2)))?;

    let mut line = Line::from_coords(1., 2., 0., 10., 20., 0.);
    line.common.layer = geometry.into();
    doc.add_entity(EntityType::Line(line))?;
    let mut circle = Circle::from_coords(-2., 4., 1., 3.5);
    circle.common.layer = geometry.into();
    doc.add_entity(EntityType::Circle(circle))?;
    let mut text = Text::with_value(label, Vector3::new(3., 5., 0.)).with_height(2.5);
    text.common.layer = notes.into();
    doc.add_entity(EntityType::Text(text))?;
    let mut writer = DxfWriter::new(&doc);
    writer.set_binary(binary);
    writer.write_to_file(path)
}

fn verify(before: &str, after: &str, binary: bool) -> acadrust::Result<()> {
    let (geometry, notes, updated_text) = if binary {
        for path in [before, after] {
            assert!(std::fs::read(path)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0"));
        }
        ("几何", "标注", "二层平面图 ^J")
    } else {
        ("Geometry", "Notes", "修订 B")
    };
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(original.version, saved.version);
    assert_eq!(original.layers.len(), saved.layers.len());
    assert_eq!(saved.layers.len(), 3);
    for name in ["0", geometry, notes] {
        assert!(saved.layers.get(name).is_some());
    }
    for layer in original.layers.iter() {
        let actual = saved.layers.get(&layer.name).unwrap();
        assert_eq!(actual.handle, layer.handle);
        let expected = if layer.name == geometry {
            Color::Index(5)
        } else {
            layer.color
        };
        assert_eq!(actual.color, expected);
        assert_eq!(actual.line_type, layer.line_type);
    }
    assert_eq!(original.entities().count(), 3);
    assert_eq!(saved.entities().count(), 3);
    let mut seen = [0; 3];
    for source in original.entities() {
        let entity = saved
            .entities()
            .find(|e| e.common().handle == source.common().handle)
            .expect("entity handle must survive the save");
        assert_eq!(entity.common().owner_handle, source.common().owner_handle);
        assert!(saved.layers.get(&entity.common().layer).is_some());
        match (source, entity) {
            (EntityType::Line(a), EntityType::Line(b)) => {
                seen[0] += 1;
                assert_eq!(b.common.layer, geometry);
                assert_eq!(b.start, a.start);
                assert_eq!(b.end, Vector3::new(25., -1.5, 2.));
            }
            (EntityType::Circle(a), EntityType::Circle(b)) => {
                seen[1] += 1;
                assert_eq!(b.common.layer, geometry);
                assert_eq!(b.center, a.center);
                assert_eq!(b.radius, 7.25);
            }
            (EntityType::Text(a), EntityType::Text(b)) => {
                seen[2] += 1;
                assert_eq!(b.common.layer, geometry);
                assert_eq!(b.insertion_point, a.insertion_point);
                assert_eq!(b.height, a.height);
                assert_eq!(b.value, updated_text);
            }
            _ => panic!("unexpected entity type"),
        }
    }
    assert_eq!(seen, [1, 1, 1]);
    println!(
        "Rust read-back passed: {} layers, LINE, CIRCLE, TEXT, handles and owners",
        saved.layers.len()
    );
    Ok(())
}

fn generate_gbk(path: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::with_version(DxfVersion::AC1018);
    doc.header.code_page = "ANSI_936".into();
    doc.layers.add(Layer::with_color("建筑", Color::Index(3)))?;
    doc.layers.add(Layer::with_color("标注", Color::Index(2)))?;
    let mut text = Text::with_value("一层平面图", Vector3::new(3., 5., 0.)).with_height(2.5);
    text.common.layer = "建筑".into();
    doc.add_entity(EntityType::Text(text))?;
    DxfWriter::new(&doc).write_to_file(path)
}

fn verify_gbk(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    for doc in [&original, &saved] {
        assert_eq!(doc.version, DxfVersion::AC1018);
        assert_eq!(doc.header.code_page, "ANSI_936");
        assert_eq!(doc.layers.len(), 3);
        assert_eq!(doc.entities().count(), 1);
    }
    for name in ["0", "建筑", "标注"] {
        let original_layer = original.layers.get(name).unwrap();
        let saved_layer = saved.layers.get(name).unwrap();
        assert_eq!(original_layer.handle, saved_layer.handle);
        assert_eq!(original_layer.color, saved_layer.color);
    }
    let EntityType::Text(source) = original.entities().next().unwrap() else {
        panic!("expected TEXT")
    };
    let EntityType::Text(result) = saved.entities().next().unwrap() else {
        panic!("expected TEXT")
    };
    assert_eq!(source.common.layer, "建筑");
    assert_eq!(source.value, "一层平面图");
    assert_eq!(result.common.layer, "标注");
    assert_eq!(result.value, "二层平面图 😀");
    assert_eq!(result.insertion_point, source.insertion_point);
    assert_eq!(result.height, source.height);
    assert_eq!(result.common.handle, source.common.handle);
    assert_eq!(result.common.owner_handle, source.common.owner_handle);
    println!(
        "Rust GBK read-back passed: AC1018 / ANSI_936, layer {}, text {}",
        result.common.layer, result.value
    );
    Ok(())
}

fn main() -> acadrust::Result<()> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    match args.as_slice() {
        [mode, format, prefix] if mode == "verify-defaults" => defaults::verify(format, prefix),
        [mode, prefix] if mode == "verify-symbol-operations" => symbol_operations::verify(prefix),
        [mode, prefix, format] if mode == "verify-review" => review::verify(prefix, format),
        [mode, prefix] if mode == "verify-reference-operations" => reference_operations::verify(prefix),
        [mode, manifest] if mode == "verify-encoding-conversion" => encoding_conversion::verify(manifest),
        [mode, prefix] if mode == "verify-stage2" => stage2::verify(prefix),
        [mode, format, path] if mode == "verify-add-entities" => add_entities::verify(format, path),
        [mode, before, prefix] if mode == "verify-remove-entities" => remove_entities::verify(before, prefix),
        [mode, before, after] if mode == "verify-remove-polylines" => remove_polylines::verify(before, after),
        [mode, prefix, space] if mode == "verify-remove-lwpolyline" => remove_extended_entities::verify(prefix, space,"lwpolyline"),
        [mode, prefix, space] if mode == "verify-remove-ellipse" => remove_extended_entities::verify(prefix, space,"ellipse"),
        [mode, prefix, space] if mode == "verify-remove-arc" => remove_extended_entities::verify(prefix, space,"arc"),
        [mode, prefix] if mode == "verify-copy-polyline2d" => copy_polylines::verify(prefix,"polyline2d"),
        [mode, prefix] if mode == "verify-copy-polyface_mesh" => copy_polylines::verify(prefix,"polyface_mesh"),
        [mode, prefix] if mode == "verify-copy-polygon_mesh" => copy_polylines::verify(prefix,"polygon_mesh"),
        [mode, prefix] if mode == "verify-copy-polyline3d" => copy_polylines::verify(prefix,"polyline3d"),
        [mode, prefix] if mode == "verify-copy-entities" => copy_entities::verify(prefix),
        [mode, prefix] if mode == "verify-extension-delete" => extension_delete::verify(prefix),
        [mode, prefix] if mode == "verify-reactor-cleanup" => reactor_cleanup::verify(prefix),
        [mode, path] if mode == "generate-defaults" => defaults::generate(path),
        [mode, before, after] if mode == "verify-add-symbols" => add_symbols::verify(before, after),
        [mode, drawing, result] if mode == "verify-symbols" => symbols::verify(drawing, result),
        [mode, path] if mode == "generate-name-map" => symbols::generate_map(path),
        [mode, ascii, binary] if mode == "generate-symbols" => symbols::generate(ascii, binary),
        [mode, prefix] if mode == "verify-ownership" => ownership::verify(prefix),
        [mode, drawing, prefix] if mode == "verify-index" => index::verify(drawing, prefix),
        [mode, ascii, binary] if mode == "generate-spaces" => spaces::generate(ascii, binary),
        [mode, drawing, result] if mode == "verify-spaces" => spaces::verify(drawing, result),
        [mode, prefix] if mode == "verify-handles" => handles::verify(prefix),
        [mode, ascii, binary] if mode == "generate-entity_properties" => entity_properties::generate(ascii, binary),
        [mode, before, after] if mode == "verify-entity_properties" => entity_properties::verify(before, after),
        [mode, input, result] if mode == "verify-transforms" => transforms::verify(input, result),
        [mode, input, result] if mode == "verify-vectors" => vectors::verify(input, result),
        [mode, ascii, binary] if mode == "generate-colors" => colors::generate(ascii, binary),
        [mode, before, after] if mode == "verify-colors" => colors::verify(before, after),
        [mode, ascii, binary] if mode == "generate-header" => header::generate(ascii, binary),
        [mode, input, result] if mode == "verify-header" => header::verify(input, result),
        [mode, ascii, binary] if mode == "generate-lwpolyline" => {
            lwpolyline::generate(ascii, binary)
        }
        [mode, before, after] if mode == "verify-lwpolyline" => lwpolyline::verify(before, after),
        [mode, ascii, binary] if mode == "generate-polyline2d" => {
            polyline2d::generate(ascii, binary)
        }
        [mode, before, after] if mode == "verify-polyline2d" => polyline2d::verify(before, after),
        [mode, ascii, binary] if mode == "generate-polyline3d" => {
            polyline3d::generate(ascii, binary)
        }
        [mode, before, after] if mode == "verify-polyline3d" => polyline3d::verify(before, after),
        [mode, ascii, binary] if mode == "generate-polygon_mesh" => polygon_mesh::generate(ascii, binary),
        [mode, before, after] if mode == "verify-polygon_mesh" => polygon_mesh::verify(before, after),
        [mode, ascii, binary] if mode == "generate-polyface_mesh" => polyface_mesh::generate(ascii, binary),
        [mode, before, after] if mode == "verify-polyface_mesh" => polyface_mesh::verify(before, after),
        [mode, ascii, binary] if mode == "generate-curves" => curves::generate(ascii, binary),
        [mode, before, after] if mode == "verify-curves" => curves::verify(before, after),
        [mode, path] if mode == "generate-binary" => generate(path, true),
        [mode, before, after] if mode == "verify-binary" => verify(before, after, true),
        [mode, path] if mode == "generate-gbk" => generate_gbk(path),
        [mode, before, after] if mode == "verify-gbk" => verify_gbk(before, after),
        [mode, path] if mode == "generate" => generate(path, false),
        [mode, before, after] if mode == "verify" => verify(before, after, false),
        _ => panic!("expected generate INPUT.dxf or verify INPUT.dxf OUTPUT.dxf"),
    }
}
