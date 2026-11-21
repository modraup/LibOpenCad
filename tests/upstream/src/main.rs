mod dwg_sat_integrity;
mod dwg_recover;
mod dwg_password;
mod dwg_mmap;
mod dwg_review_regressions;
mod dwg_history;
mod dwg_history_author;
mod dwg_dynamic;
mod dwg_associative;
mod dwg_lifecycle;
mod dwg_history_metadata;
mod dwg_surface;
mod dwg_modeler;
mod acis;
mod acis_spline;
mod modeler;
mod surface;
mod surface_profiles;
mod sync_20261115;
mod solid_history;
mod history_author;
mod history_profiles;
mod history_merge;
mod modeler_explode;
mod expand_specialized;
mod dynamic_properties;
mod dynamic_objects;
mod dynamic_entities;
mod dwg_dynamic_entities;
mod architecture;
mod class_objects;
mod object_data;
mod associative;
mod extended_transform;
mod ole_transform;
mod table_transform;
mod viewport_transform;
mod raster_transform;
mod shape_underlay_transform;
mod hatch_transform;
mod dimension_transform;
mod annotation_transform;
mod audit;
mod expand_insert;
mod text_transform;
mod mesh;
mod entity_explode;
mod entity_transform;
mod geometry;
mod dwg_r2000;
mod dwg_families;
mod hatch_versions;
mod mlatt;
mod r13_polyline;
mod version_convert;
mod upstream_sync;
mod r12_export;
mod approximation;
mod object_versions;
mod upgrade_r12;
mod version_matrix;
mod stage3_integration;
mod data_objects;
mod object_contexts;
mod more_extended;
// SPDX-License-Identifier: MPL-2.0
mod symbol_operations;
mod basic_geometry;
mod faces;
mod splines;
mod mtext;
mod blocks;
mod attributes;
mod layouts;
mod xdata;
mod table_definitions;
mod text_styles;
mod dim_styles;
mod annotations;
mod hatches;
mod dimensions;
mod mlines;
mod multileaders;
mod tables;
mod table_content;
mod table_geometry;
mod cell_styles;
mod external_references;
mod shapes_common;
mod materials;
mod fields;
mod proxies;
mod proxy_commands;
mod auxiliary_objects;
mod extended_entities;
mod drawing_objects;
mod registered_classes;
mod mtext_format;
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
        [m,v,p] if m=="verify-dwg-password" => dwg_password::verify(v,p),
        [m,v,p] if m=="verify-dwg-mmap" => dwg_mmap::verify(v,p),
        [m,v,p] if m=="verify-dwg-recover" => dwg_recover::verify(v,p),
        [m,v,p] if m=="generate-dwg-layer-boundary" => dwg_families::layer_seed_fixture(v,p),
        [m,v,p] if m=="generate-dwg-shared" => dwg_families::shared_fixture(v,p),
        [m,v,p] if m=="generate-dwg-text-style-boundary" => dwg_families::text_style_boundary(v,p),
        [m,v,c,k,p] if m=="generate-dwg-text-common" => dwg_families::text_common_fixture(v,c,k=="book",p),
        [m,p,b] if m=="verify-dwg-text-common" => dwg_families::verify_text_common(p,b),
        [m,p,b] if m=="verify-dwg-text-geometry" => dwg_families::verify_text_geometry(p,b),
        [m,p,b] if m=="verify-dwg-text-format" => dwg_families::verify_text_format(p,b),
        [m,v,c,p] if m=="generate-dwg-text" => dwg_families::text_fixture(v,c,p),
        [m,p,b] if m=="verify-dwg-text" => dwg_families::verify_text(p,b),
        [m,p,b] if m=="verify-dwg-transparency" => dwg_families::verify_entity_transparency(p,b),
        [m,v,p] if m=="generate-dwg-transparency" => dwg_families::transparency_fixture(v,p),
        [m,p,b] if m=="verify-dwg-scale" => dwg_families::verify_entity_scale(p,b),
        [m,v,p] if m=="generate-dwg-scale" => dwg_families::scale_fixture(v,p),
        [m,p,b] if m=="verify-dwg-display" => dwg_families::verify_entity_display(p,b),
        [m,p,b] if m=="verify-dwg-color" => dwg_families::verify_entity_color(p,b),
        [m,p] if m=="generate-dwg-color-book" => dwg_families::color_book_fixture(p),
        [m,p,b] if m=="verify-dwg-entity-linetype" => dwg_families::verify_entity_linetype(p,b),
        [m,p,b] if m=="verify-dwg-linetype" => dwg_families::verify_added_linetype(p,b),
        [m,p,b] if m=="verify-dwg-layer" => dwg_families::verify_added_layer(p,b),
        [m,v,p] if m=="generate-dwg-compound" => dwg_families::compound_fixture(v,p),
        [m,p] if m=="verify-expand-specialized" => expand_specialized::verify(p),
        [m,a] if m=="generate-acis-spline" => Ok(acis_spline::generate(a)),
        [m,a,b] if m=="verify-acis-spline" => acis_spline::verify_suite(a,b),
        [m,a] if m=="generate-modeler-explode" => modeler_explode::generate(a),
        [m,a,b] if m=="verify-modeler-explode" => modeler_explode::verify(a,b),
        [m,a] if m=="verify-history-merge" => history_merge::verify(a),
        [m,a,b] if m=="generate-history-profiles" => history_profiles::generate(a,b),
        [m,a,b] if m=="verify-history-profiles" => history_profiles::verify(a,b),
        [m,a,b] if m=="generate-history-author" => history_author::generate(a,b),
        [m,a,b] if m=="verify-history-author" => history_author::verify(a,b),
        [m,a] if m=="generate-solid-history" => solid_history::generate(a),
        [m,a,b] if m=="verify-solid-history" => solid_history::verify(a,b),
        [m,a] if m=="verify-sync-20261115" => sync_20261115::verify(a),
        [m,a,b] if m=="generate-surface-profiles" => surface_profiles::generate(a,b),
        [m,a,b] if m=="verify-surface-profiles" => surface_profiles::verify(a,b),
        [m,a] if m=="generate-surface" => surface::generate(a),
        [m,a,b] if m=="verify-surface" => surface::verify(a,b),
        [m,a,b] if m=="generate-dwg-history" => dwg_history::generate(a,b),
        [m,a,b] if m=="verify-dwg-history-metadata" => dwg_history_metadata::verify(a,b),
        [m,a,b] if m=="generate-dwg-review" => dwg_review_regressions::generate(a,b),
        [m,a,b] if m=="verify-dwg-sat" => dwg_sat_integrity::verify(a,b),
        [m,a,b,c] if m=="verify-dwg-review" => dwg_review_regressions::verify(a,b,c),
        [m,a,b] if m=="verify-dwg-lifecycle" => dwg_lifecycle::verify(a,b),
        [m,a,b] if m=="generate-dwg-lifecycle" => dwg_lifecycle::generate(a,b),
        [m,a,b] if m=="verify-dwg-history" => dwg_history::verify(a,b),
        [m,a,b] if m=="verify-dwg-history-author" => dwg_history_author::verify(a,b),
        [m,a,b] if m=="generate-dwg-dynamic" => dwg_dynamic::generate(a,b),
        [m,a,b] if m=="verify-dwg-dynamic" => dwg_dynamic::verify(a,b),
        [m,a] if m=="generate-dwg-associative" => dwg_associative::generate(a),
        [m,a,b] if m=="verify-dwg-associative" => dwg_associative::verify(a,b),
        [m,a] if m=="generate-dwg-surface" => dwg_surface::generate(a),
        [m,a,b] if m=="verify-dwg-surface" => dwg_surface::verify(a,b),
        [m,a] if m=="generate-dwg-modeler" => dwg_modeler::generate(a),
        [m,a,b] if m=="verify-dwg-modeler-empty" => dwg_modeler::verify_empty(a,b),
        [m,a,b] if m=="verify-dwg-modeler" => dwg_modeler::verify(a,b),
        [m,a] if m=="generate-modeler" => modeler::generate(a),
        [m,a,b] if m=="verify-modeler" => modeler::verify(a,b),
        [m,a] if m=="generate-acis-sab" => Ok(acis::generate_sab(a)),
        [m,a,b] if m=="verify-acis-sab" => Ok(acis::verify_sab(a,b)),
        [m,a] if m=="generate-acis" => Ok(acis::generate(a)),
        [m,a,b] if m=="verify-acis" => Ok(acis::verify(a,b)),
        [m,a,b] if m=="generate-dynamic-properties" => dynamic_properties::generate(a,b),
        [m,a,b] if m=="verify-dynamic-properties" => dynamic_properties::verify(a,b),
        [m,a,b] if m=="verify-dynamic-objects" => dynamic_objects::verify(a,b),
        [m,a] if m=="verify-dynamic-entities" => dynamic_entities::verify(a),
        [m,a] if m=="verify-dwg-dynamic-entities" => dwg_dynamic_entities::verify(a),
        [m,a] if m=="verify-architecture" => architecture::verify(a),
        [m,a] if m=="verify-class-objects" => class_objects::verify(a),
        [m,a] if m=="verify-object-data" => object_data::verify(a),
        [m,a] if m=="generate-associative" => associative::generate(a),
        [m,a,b] if m=="verify-associative" => associative::verify(a,b),
        [m,a,b] if m=="generate-extended-transform" => extended_transform::generate(a,b),
        [m,a,b] if m=="verify-extended-transform" => extended_transform::verify(a,b),
        [m,a,b] if m=="generate-ole-transform" => ole_transform::generate(a,b),
        [m,a,b] if m=="verify-ole-transform" => ole_transform::verify(a,b),
        [m,a,b] if m=="generate-table-transform" => table_transform::generate(a,b),
        [m,a,b] if m=="verify-table-transform" => table_transform::verify(a,b),
        [m,a,b] if m=="generate-viewport-transform" => viewport_transform::generate(a,b),
        [m,a,b] if m=="verify-viewport-transform" => viewport_transform::verify(a,b),
        [m,a,b] if m=="generate-raster-transform" => raster_transform::generate(a,b),
        [m,a,b] if m=="verify-raster-transform" => raster_transform::verify(a,b),
        [m,a,b] if m=="generate-shape-underlay-transform" => shape_underlay_transform::generate(a,b),
        [m,a,b] if m=="verify-shape-underlay-transform" => shape_underlay_transform::verify(a,b),
        [m,a,b] if m=="generate-hatch-transform" => hatch_transform::generate(a,b),
        [m,a,b] if m=="verify-hatch-transform" => hatch_transform::verify(a,b),
        [m,a,b] if m=="generate-dimension-transform" => dimension_transform::generate(a,b),
        [m,a,b] if m=="verify-dimension-transform" => dimension_transform::verify(a,b),
        [m,a,b] if m=="generate-annotation-transform" => annotation_transform::generate(a,b),
        [m,a,b] if m=="verify-annotation-transform" => annotation_transform::verify(a,b),
        [m,a,b] if m=="generate-text-transform" => text_transform::generate(a,b),
        [m,a,b] if m=="verify-text-transform" => text_transform::verify(a,b),
        [m,a,b] if m=="generate-entity-transform" => entity_transform::generate(a,b),
        [m,p] if m=="verify-audit" => audit::verify(p),
        [m,p] if m=="verify-expand-insert" => expand_insert::verify(p),
        [m,p] if m=="verify-mesh" => mesh::verify(p),
        [m,a,b] if m=="verify-entity-explode" => entity_explode::verify(a,b),
        [m,a,b] if m=="verify-entity-transform" => entity_transform::verify(a,b),
        [m,a,b] if m=="generate-geometry" => geometry::generate(a,b),
        [m,a,b] if m=="verify-geometry" => geometry::verify(a,b),
        [m,p,b] if m=="verify-dwg-compound-move" => dwg_families::verify_compound_move(p,b),
        [m,p] if m=="verify-dwg-text-create" => dwg_families::verify_text_create(p),
        [m,p,b] if m=="verify-dwg-text-move" => dwg_families::verify_text_move(p,b),
        [m,p,b] if m=="verify-dwg-move" => dwg_families::verify_move(p,b),
        [m,p,b] if m=="verify-dwg-shared" => dwg_families::shared_verify(p,b),
        [m,p] if m=="generate-dwg-block" => dwg_families::block_fixture(p),
        [m,p,b] if m=="verify-dwg-block" => dwg_families::verify_block(p,b),
        [m,v,p] if m=="generate-dwg-blank" => dwg_families::blank(v,p),
        [m,v,p] if m=="generate-dwg-family" => dwg_families::generate(v,p),
        [m,op,p,t,reference] if m=="verify-dwg-document" => dwg_families::document(op,p,t,reference),
        [m,p,t] if m=="verify-dwg-family" => dwg_families::verify(p,t),
        [m,p,t] if m=="verify-dwg-sections" => dwg_families::verify_sections(p,t),
        [m,p] if m=="generate-dwg-r2000-lines" => dwg_r2000::generate_lines(p),
        [m,p] if m=="generate-dwg-r2000" => dwg_r2000::generate(p),
        [m,p,t] if m=="verify-dwg-r2000" => dwg_r2000::verify(p,t,true),
        [m,p,t] if m=="verify-external-dwg-r2000" => dwg_r2000::verify(p,t,false),
        [mode, prefix] if mode == "verify-hatch-versions" => hatch_versions::verify(prefix),
        [mode, prefix] if mode == "verify-mlatt" => mlatt::verify(prefix),
        [mode, prefix] if mode == "verify-r13-polyline" => r13_polyline::verify(prefix),
        [mode, prefix] if mode == "verify-version-convert" => version_convert::verify(prefix),
        [mode, path] if mode == "generate-sync-payload" => upstream_sync::generate(path),
        [mode, prefix, payload] if mode == "verify-upstream-sync" => upstream_sync::verify(prefix, payload),
        [mode, prefix] if mode == "verify-upgrade-r12" => upgrade_r12::verify(prefix),
        [mode, ascii, binary] if mode == "generate-class-versions" => object_versions::generate(ascii,binary),
        [mode, prefix] if mode == "verify-object-versions" => object_versions::verify(prefix),
        [mode,prefix] if mode=="verify-current-object-versions" => object_versions::verify(prefix),
        [mode, prefix] if mode == "verify-approximation" => approximation::verify(prefix),
        [mode, prefix] if mode == "verify-r12-export" => r12_export::verify(prefix),
        [mode, prefix] if mode == "verify-version-matrix" => version_matrix::verify(prefix),
        [mode, prefix] if mode == "verify-read-diagnostics" => version_matrix::diagnostics(prefix),
        [mode, input, output] if mode == "verify-mtext-format" => mtext_format::verify(input, output),
        [mode, prefix] if mode == "verify-stage3-integration" => stage3_integration::verify(prefix),
        [mode, ascii, binary] if mode == "generate-data-objects" => data_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-data-objects" => data_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-drawing-objects" => drawing_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-drawing-objects" => drawing_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-extended-entities" => extended_entities::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-extended-entities" => extended_entities::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-auxiliary-objects" => auxiliary_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-auxiliary-objects" => auxiliary_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-proxies" => proxies::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-proxies" => proxies::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-fields" => fields::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-fields" => fields::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-materials" => materials::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-materials" => materials::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-shapes-common" => shapes_common::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-shapes-common" => shapes_common::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-external-references" => external_references::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-external-references" => external_references::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-cell-styles" => cell_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-cell-styles" => cell_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-geometry" => table_geometry::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-geometry" => table_geometry::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-content" => table_content::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-content" => table_content::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-tables" => tables::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-tables" => tables::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-multileaders" => multileaders::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-multileaders" => multileaders::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-mlines" => mlines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-mlines" => mlines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dimensions" => dimensions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dimensions" => dimensions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-hatches" => hatches::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-hatches" => hatches::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-annotations" => annotations::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-annotations" => annotations::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dim-styles" => dim_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dim-styles" => dim_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-text-styles" => text_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-text-styles" => text_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-definitions" => table_definitions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-definitions" => table_definitions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-xdata" => xdata::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-xdata" => xdata::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-layouts" => layouts::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-layouts" => layouts::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-attributes" => attributes::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-attributes" => attributes::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-blocks" => blocks::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-blocks" => blocks::verify(input, prefix),
        [mode, prefix] if mode == "generate-mtext" => mtext::generate(prefix),
        [mode, input, prefix] if mode == "verify-mtext" => mtext::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-splines" => splines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-splines" => splines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-faces" => faces::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-faces" => faces::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-basic-geometry" => basic_geometry::generate(ascii, binary),
        [mode, prefix] if mode == "verify-basic-geometry" => basic_geometry::verify(prefix),
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

        [mode, ascii, binary] if mode == "generate-object-contexts" => object_contexts::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-object-contexts" => object_contexts::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-drawing-objects" => drawing_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-drawing-objects" => drawing_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-extended-entities" => extended_entities::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-extended-entities" => extended_entities::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-auxiliary-objects" => auxiliary_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-auxiliary-objects" => auxiliary_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-proxies" => proxies::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-proxies" => proxies::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-fields" => fields::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-fields" => fields::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-materials" => materials::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-materials" => materials::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-shapes-common" => shapes_common::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-shapes-common" => shapes_common::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-external-references" => external_references::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-external-references" => external_references::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-cell-styles" => cell_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-cell-styles" => cell_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-geometry" => table_geometry::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-geometry" => table_geometry::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-content" => table_content::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-content" => table_content::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-tables" => tables::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-tables" => tables::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-multileaders" => multileaders::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-multileaders" => multileaders::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-mlines" => mlines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-mlines" => mlines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dimensions" => dimensions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dimensions" => dimensions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-hatches" => hatches::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-hatches" => hatches::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-annotations" => annotations::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-annotations" => annotations::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dim-styles" => dim_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dim-styles" => dim_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-text-styles" => text_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-text-styles" => text_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-definitions" => table_definitions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-definitions" => table_definitions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-xdata" => xdata::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-xdata" => xdata::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-layouts" => layouts::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-layouts" => layouts::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-attributes" => attributes::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-attributes" => attributes::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-blocks" => blocks::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-blocks" => blocks::verify(input, prefix),
        [mode, prefix] if mode == "generate-mtext" => mtext::generate(prefix),
        [mode, input, prefix] if mode == "verify-mtext" => mtext::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-splines" => splines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-splines" => splines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-faces" => faces::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-faces" => faces::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-basic-geometry" => basic_geometry::generate(ascii, binary),
        [mode, prefix] if mode == "verify-basic-geometry" => basic_geometry::verify(prefix),
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

        [mode, ascii, binary] if mode == "generate-more-extended" => more_extended::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-more-extended" => more_extended::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-drawing-objects" => drawing_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-drawing-objects" => drawing_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-extended-entities" => extended_entities::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-extended-entities" => extended_entities::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-auxiliary-objects" => auxiliary_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-auxiliary-objects" => auxiliary_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-proxies" => proxies::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-proxies" => proxies::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-fields" => fields::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-fields" => fields::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-materials" => materials::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-materials" => materials::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-shapes-common" => shapes_common::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-shapes-common" => shapes_common::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-external-references" => external_references::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-external-references" => external_references::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-cell-styles" => cell_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-cell-styles" => cell_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-geometry" => table_geometry::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-geometry" => table_geometry::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-content" => table_content::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-content" => table_content::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-tables" => tables::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-tables" => tables::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-multileaders" => multileaders::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-multileaders" => multileaders::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-mlines" => mlines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-mlines" => mlines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dimensions" => dimensions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dimensions" => dimensions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-hatches" => hatches::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-hatches" => hatches::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-annotations" => annotations::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-annotations" => annotations::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dim-styles" => dim_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dim-styles" => dim_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-text-styles" => text_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-text-styles" => text_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-definitions" => table_definitions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-definitions" => table_definitions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-xdata" => xdata::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-xdata" => xdata::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-layouts" => layouts::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-layouts" => layouts::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-attributes" => attributes::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-attributes" => attributes::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-blocks" => blocks::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-blocks" => blocks::verify(input, prefix),
        [mode, prefix] if mode == "generate-mtext" => mtext::generate(prefix),
        [mode, input, prefix] if mode == "verify-mtext" => mtext::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-splines" => splines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-splines" => splines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-faces" => faces::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-faces" => faces::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-basic-geometry" => basic_geometry::generate(ascii, binary),
        [mode, prefix] if mode == "verify-basic-geometry" => basic_geometry::verify(prefix),
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

        [mode, ascii, binary] if mode == "generate-registered-classes" => registered_classes::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-registered-classes" => registered_classes::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-drawing-objects" => drawing_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-drawing-objects" => drawing_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-extended-entities" => extended_entities::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-extended-entities" => extended_entities::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-auxiliary-objects" => auxiliary_objects::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-auxiliary-objects" => auxiliary_objects::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-proxies" => proxies::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-proxies" => proxies::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-fields" => fields::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-fields" => fields::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-materials" => materials::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-materials" => materials::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-shapes-common" => shapes_common::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-shapes-common" => shapes_common::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-external-references" => external_references::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-external-references" => external_references::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-cell-styles" => cell_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-cell-styles" => cell_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-geometry" => table_geometry::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-geometry" => table_geometry::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-content" => table_content::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-content" => table_content::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-tables" => tables::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-tables" => tables::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-multileaders" => multileaders::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-multileaders" => multileaders::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-mlines" => mlines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-mlines" => mlines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dimensions" => dimensions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dimensions" => dimensions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-hatches" => hatches::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-hatches" => hatches::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-annotations" => annotations::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-annotations" => annotations::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-dim-styles" => dim_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-dim-styles" => dim_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-text-styles" => text_styles::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-text-styles" => text_styles::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-table-definitions" => table_definitions::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-table-definitions" => table_definitions::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-xdata" => xdata::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-xdata" => xdata::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-layouts" => layouts::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-layouts" => layouts::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-attributes" => attributes::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-attributes" => attributes::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-blocks" => blocks::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-blocks" => blocks::verify(input, prefix),
        [mode, prefix] if mode == "generate-mtext" => mtext::generate(prefix),
        [mode, input, prefix] if mode == "verify-mtext" => mtext::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-splines" => splines::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-splines" => splines::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-faces" => faces::generate(ascii, binary),
        [mode, input, prefix] if mode == "verify-faces" => faces::verify(input, prefix),
        [mode, ascii, binary] if mode == "generate-basic-geometry" => basic_geometry::generate(ascii, binary),
        [mode, prefix] if mode == "verify-basic-geometry" => basic_geometry::verify(prefix),
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
