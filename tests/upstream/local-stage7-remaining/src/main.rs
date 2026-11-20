// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 remaining subsets).
use acadrust::compound_file::CompoundPropertySetStream;
use acadrust::count::{group_matches, similar_matches};
use acadrust::entities::EntityType;
use acadrust::fields::{self, FieldContext};
use acadrust::objects::{KnownXRecordKind, ObjectType, XRecordValue};
use acadrust::{
    DxfReader, SemanticEntityV1, SemanticPartV1, SEMANTIC_INVENTORY_VERSION,
};
use std::env;
use std::f64::consts::PI;
use std::fs;
use std::path::Path;

struct TestContext;
impl FieldContext for TestContext {
    fn now_julian(&self) -> f64 {
        2_460_000.5
    }
}

fn read_dxf(path: &Path) -> acadrust::CadDocument {
    DxfReader::from_file(path).unwrap().read().unwrap()
}

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");

    let props = fs::read(Path::new(&format!("{prefix}-props.bin"))).unwrap();
    let stream = CompoundPropertySetStream::decode(&props).expect("property set");
    let set = &stream.sets[0];
    let hello = set
        .properties
        .iter()
        .find(|p| p.id == 2)
        .expect("ansi");
    match &hello.value {
        acadrust::compound_file::CompoundPropertyValue::AnsiString { value, .. } => {
            assert_eq!(value, "Hello")
        }
        other => panic!("expected ansi {other:?}"),
    }
    let sheets = set
        .properties
        .iter()
        .find(|p| p.id == 4)
        .expect("vector");
    match &sheets.value {
        acadrust::compound_file::CompoundPropertyValue::Vector { values, .. } => {
            assert_eq!(values.len(), 2);
        }
        other => panic!("expected vector {other:?}"),
    }

    let lpstr = fs::read(Path::new(&format!("{prefix}-lpstr.bin"))).unwrap();
    let raw = CompoundPropertySetStream::decode(&lpstr).expect("unpadded");
    let vector = raw.sets[0]
        .properties
        .iter()
        .find_map(|p| match &p.value {
            acadrust::compound_file::CompoundPropertyValue::Vector { values, .. } => Some(values),
            _ => None,
        })
        .expect("lpstr vector");
    assert_eq!(vector.len(), 2);

    let fields = read_dxf(Path::new(&format!("{prefix}-expr.dxf")));
    let host = fields
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::MText(text) => Some(text.common.handle),
            _ => None,
        })
        .expect("expr host");
    assert_eq!(
        fields::resolve(&fields, host, &TestContext).as_deref(),
        Some("4.000000")
    );

    let area = read_dxf(Path::new(&format!("{prefix}-fields.dxf")));
    let host = area
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::MText(text) => Some(text.common.handle),
            _ => None,
        })
        .expect("area host");
    let shown = fields::resolve(&area, host, &TestContext).expect("area");
    let n: f64 = shown.parse().expect("area number");
    assert!((n - std::f64::consts::PI).abs() < 1e-5, "{shown}");

    let count = read_dxf(Path::new(&format!("{prefix}-count.dxf")));
    let square = count
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::LwPolyline(pl) if pl.is_closed => Some(pl.common.handle),
            _ => None,
        })
        .expect("square");
    assert_eq!(similar_matches(&count, count.get_entity(square).unwrap(), None).len(), 2);
    let lines: Vec<_> = count
        .model_space_entities()
        .filter_map(|entity| match entity {
            EntityType::Line(line) => Some(line.common.handle),
            _ => None,
        })
        .collect();
    assert_eq!(group_matches(&count, &lines, None).len(), 1);
    let spline = count
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::Spline(s) => Some(s.common.handle),
            _ => None,
        })
        .expect("spline");
    assert_eq!(similar_matches(&count, count.get_entity(spline).unwrap(), None).len(), 2);

    let inserts = read_dxf(Path::new(&format!("{prefix}-insert.dxf")));
    let ring = inserts
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::LwPolyline(pl) if pl.is_closed => Some(pl.common.handle),
            _ => None,
        })
        .expect("boundary");
    assert_eq!(
        acadrust::count::evaluate(
            &inserts,
            &format!(
                "\\AcCount {{\"name\":\"Stick\",\"type\":\"block\",\"boundaryObjectHandle\":\"{:X}\"}}",
                ring.value()
            )
        ),
        "1"
    );
    assert_eq!(
        acadrust::count::evaluate(
            &inserts,
            "\\AcCount {\"name\":\"Stick\",\"type\":\"block\",\"key\":{\"layer\":\"A\"}}"
        ),
        "1"
    );

    let picture = read_dxf(Path::new(&format!("{prefix}-picture.dxf")));
    let host = picture
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::MText(text) => Some(text.common.handle),
            _ => None,
        })
        .expect("picture host");
    assert_eq!(
        fields::resolve(&picture, host, &TestContext).as_deref(),
        Some("0.50")
    );

    let objprop = read_dxf(Path::new(&format!("{prefix}-objprop.dxf")));
    let host = objprop
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::MText(text) => Some(text.common.handle),
            _ => None,
        })
        .expect("objprop host");
    assert_eq!(
        fields::resolve(&objprop, host, &TestContext).as_deref(),
        Some("Hello")
    );

    let units = read_dxf(Path::new(&format!("{prefix}-units.dxf")));
    let mut resolved = Vec::new();
    for entity in units.model_space_entities() {
        if let EntityType::MText(text) = entity {
            if let Some(value) = fields::resolve(&units, text.common.handle, &TestContext) {
                resolved.push(value);
            }
        }
    }
    assert!(
        resolved.iter().any(|s| s == "Millimeters"),
        "InsUnits {resolved:?}"
    );
    assert!(
        resolved.iter().any(|s| s == "0.001000"),
        "InsUnitsFactor {resolved:?}"
    );
    assert!(
        resolved.iter().any(|s| s == "2000.000000"),
        "XEffectiveScaleFactor {resolved:?}"
    );
    assert!(
        resolved
            .iter()
            .any(|s| s == "11.000000, 2.000000, 3.000000"),
        "InsertionPoint WCS {resolved:?}"
    );
    assert!(
        resolved.iter().any(|s| s == "1.000000, 2.000000, 3.000000"),
        "InsertionPoint UCS {resolved:?}"
    );
    assert!(
        resolved.iter().any(|s| s == "ByLayer"),
        "Material ByLayer {resolved:?}"
    );
    assert_eq!(
        acadrust::count::evaluate(&units, r#"\AcCount {"name":"Stick","type":"block"}"#),
        "1"
    );

    match acadrust::count::parse_query(r#"{"name":"Stick","type":"block"}"#) {
        Some(acadrust::count::CountQuery::Block { name, boundary, .. }) => {
            assert_eq!(name, "Stick");
            assert!(boundary.is_none());
        }
        other => panic!("block query {other:?}"),
    }
    match acadrust::count::parse_query(r#"{"targets":["1A","2B"],"type":"single"}"#) {
        Some(acadrust::count::CountQuery::Single(targets)) => {
            assert_eq!(targets.len(), 2);
            assert_eq!(targets[0].value(), 0x1A);
        }
        other => panic!("single query {other:?}"),
    }
    assert!(acadrust::count::parse_query(r#"{"type":"nope"}"#).is_none());

    let hatches = read_dxf(Path::new(&format!("{prefix}-hatch.dxf")));
    let hatch = hatches
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::Hatch(h) => Some(h.common.handle),
            _ => None,
        })
        .expect("hatch");
    assert_eq!(
        similar_matches(&hatches, hatches.get_entity(hatch).unwrap(), None).len(),
        2
    );
    assert_eq!(
        acadrust::count::evaluate(
            &hatches,
            &format!(
                "\\AcCount {{\"targets\":[\"{:X}\"],\"type\":\"single\"}}",
                hatch.value()
            )
        ),
        "2"
    );

    assert_eq!(SEMANTIC_INVENTORY_VERSION, 1);
    let inventory = read_dxf(Path::new(&format!("{prefix}-inventory.dxf")));
    let expected: std::collections::HashMap<String, usize> =
        fs::read_to_string(Path::new(&format!("{prefix}-inventory.txt")))
            .unwrap()
            .lines()
            .filter_map(|line| {
                let (key, value) = line.split_once('=')?;
                Some((key.to_string(), value.parse().ok()?))
            })
            .collect();
    let mut headers = 0;
    let mut tables = 0;
    let mut classes = 0;
    let mut entities = 0;
    let mut objects = 0;
    let mut summaries = 0;
    let mut previews = 0;
    inventory.semantic_inventory_v1().visit(|part| match part {
        SemanticPartV1::Header(_) => headers += 1,
        SemanticPartV1::TableRecord(_) => tables += 1,
        SemanticPartV1::Class(_) => classes += 1,
        SemanticPartV1::Entity(SemanticEntityV1::Typed(_))
        | SemanticPartV1::Entity(SemanticEntityV1::Unsupported { .. }) => entities += 1,
        SemanticPartV1::Object(_) => objects += 1,
        SemanticPartV1::SummaryInfo(_) => summaries += 1,
        SemanticPartV1::Preview(_) => previews += 1,
        _ => {}
    });
    assert_eq!(headers, expected["headers"]);
    assert_eq!(tables, expected["tables"]);
    assert_eq!(classes, expected["classes"]);
    assert_eq!(entities, expected["entities"]);
    assert_eq!(objects, expected["objects"]);
    assert_eq!(summaries, expected["summaries"]);
    assert_eq!(previews, expected["previews"]);

    let tables = read_dxf(Path::new(&format!("{prefix}-table.dxf")));
    let mut texts = Vec::new();
    let mut saw_field = false;
    for entity in tables.entities() {
        let EntityType::Table(table) = entity else {
            continue;
        };
        for item in tables.entities_in_block(&table.block_name) {
            if let EntityType::MText(mtext) = item {
                texts.push(mtext.value.clone());
            }
        }
        for row in &table.rows {
            for cell in &row.cells {
                let Some(field) = cell.contents.first().and_then(|content| content.field_handle) else {
                    continue;
                };
                assert_eq!(
                    fields::cell_field_text(&tables, field, table.common.handle, &TestContext)
                        .as_deref(),
                    Some("5")
                );
                saw_field = true;
            }
        }
    }
    assert!(texts.iter().any(|s| s == "42"), "mtext 42 {texts:?}");
    assert!(texts.iter().any(|s| s == "7"), "mtext 7 {texts:?}");
    assert!(
        texts.iter().any(|s| s == "3.00 mm"),
        "mtext formatted {texts:?}"
    );
    assert!(
        texts.iter().any(|s| s == "formatted text"),
        "mtext colored {texts:?}"
    );
    assert!(saw_field, "table cell field");

    let nod = tables.named_objects_dictionary().expect("NOD");
    let rec = nod.get("ACDB_RECOMPOSE_DATA").expect("ACDB_RECOMPOSE_DATA");
    match tables.objects.get(&rec) {
        Some(ObjectType::XRecord(record)) => {
            assert!(
                record
                    .entries
                    .iter()
                    .any(|entry| entry.code == 90 && matches!(entry.value, XRecordValue::Int32(1))),
                "recompose flag"
            );
            assert!(
                record
                    .entries
                    .iter()
                    .any(|entry| entry.code == 330 && matches!(entry.value, XRecordValue::Handle(_))),
                "recompose handle"
            );
        }
            other => panic!("recompose {other:?}"),
    }

    let hosted = read_dxf(Path::new(&format!("{prefix}-textfield.dxf")));
    let mut saw_five = false;
    for entity in hosted.entities() {
        let EntityType::MText(mtext) = entity else {
            continue;
        };
        if mtext.value == "5" {
            assert_eq!(
                fields::resolve(&hosted, mtext.common.handle, &TestContext).as_deref(),
                Some("5")
            );
            saw_five = true;
        }
    }
    assert!(saw_five, "hosted field MTEXT");

    let inlined = read_dxf(Path::new(&format!("{prefix}-inline.dxf")));
    let expected_sum = format!("{:.2}", 5.0 * PI);
    let mut saw_sum = false;
    for entity in inlined.entities() {
        let EntityType::MText(mtext) = entity else {
            continue;
        };
        if fields::resolve(&inlined, mtext.common.handle, &TestContext).as_deref()
            == Some(expected_sum.as_str())
        {
            saw_sum = true;
        }
    }
    assert!(saw_sum, "inlined formula MTEXT");

    let attrs = read_dxf(Path::new(&format!("{prefix}-attrfield.dxf")));
    let mut saw_area = false;
    for entity in attrs.entities() {
        let EntityType::Insert(insert) = entity else {
            continue;
        };
        for attrib in &insert.attributes {
            if !attrib.tag.eq_ignore_ascii_case("AREA") {
                continue;
            }
            assert_eq!(
                fields::resolve(&attrs, attrib.common.handle, &TestContext).as_deref(),
                Some(expected_sum.as_str())
            );
            saw_area = true;
        }
    }
    assert!(saw_area, "attached AREA attribute field");

    let grouped = read_dxf(Path::new(&format!("{prefix}-groups.dxf")));
    let group = grouped.get_group("GroupRoundtrip").expect("GroupRoundtrip");
    assert_eq!(group.name, "GroupRoundtrip");
    assert_eq!(group.entities.len(), 1);
    let unnamed = grouped
        .groups()
        .find(|g| g.is_unnamed())
        .expect("unnamed group");
    assert!(unnamed.name.starts_with("*A"), "{}", unnamed.name);
    let member = group.entities[0];
    let entity = grouped.get_entity(member).expect("group member");
    assert!(
        entity.common().reactors.contains(&group.handle),
        "group reactor"
    );

    let meshuv = read_dxf(Path::new(&format!("{prefix}-meshuv.dxf")));
    let mesh = meshuv
        .entities()
        .find_map(|entity| match entity {
            EntityType::Mesh(mesh) => Some(mesh.common.handle),
            _ => None,
        })
        .expect("mesh");
    let uvs = meshuv.mesh_texture_coordinates(mesh);
    assert_eq!(uvs.len(), 2);
    assert!((uvs[0].x - 0.1).abs() < 1e-9);
    assert!((uvs[1].z - 0.6).abs() < 1e-9);

    let vpxrec = read_dxf(Path::new(&format!("{prefix}-vpxrec.dxf")));
    let viewport = vpxrec
        .entities()
        .find_map(|entity| match entity {
            EntityType::Viewport(viewport) => Some(viewport.common.handle),
            _ => None,
        })
        .expect("viewport");
    let layer = vpxrec.layers.get("0").expect("layer 0").handle;
    let color = vpxrec.layer_viewport_overrides(layer, KnownXRecordKind::LayerViewportColorOverride);
    assert_eq!(color.len(), 1);
    assert_eq!(color[0].0, viewport);
    match &color[0].1 {
        XRecordValue::Int32(value) => assert_eq!(*value, 2),
        other => panic!("expected int32 color override {other:?}"),
    }
    let linetype =
        vpxrec.layer_viewport_overrides(layer, KnownXRecordKind::LayerViewportLinetypeOverride);
    assert_eq!(linetype.len(), 1);
    assert!(matches!(linetype[0].1, XRecordValue::Handle(_)));
    let lineweight =
        vpxrec.layer_viewport_overrides(layer, KnownXRecordKind::LayerViewportLineweightOverride);
    assert_eq!(lineweight.len(), 1);
    match &lineweight[0].1 {
        XRecordValue::Int32(value) => assert_eq!(*value, 25),
        other => panic!("expected int32 lineweight {other:?}"),
    }
    let alpha = vpxrec.layer_viewport_overrides(layer, KnownXRecordKind::LayerViewportAlphaOverride);
    assert_eq!(alpha.len(), 1);
    match &alpha[0].1 {
        XRecordValue::Int32(value) => assert_eq!(*value, 128),
        other => panic!("expected int32 alpha {other:?}"),
    }
    let scale = vpxrec
        .viewport_annotation_scale(viewport)
        .expect("annotation scale");
    assert!(!scale.is_null());

    let recovered = DxfReader::from_file(Path::new(&format!("{prefix}-recover.dxf")))
        .unwrap()
        .recover()
        .unwrap();
    assert!(recovered.stats.has_usable_drawing_data());
    assert_eq!(recovered.document.entity_count(), 1);
    assert!(recovered.repair.after.scanned_entities > 0);

    let malformed = DxfReader::from_file(Path::new(&format!("{prefix}-malformed.dxf")))
        .unwrap()
        .recover()
        .unwrap();
    assert_eq!(malformed.document.entity_count(), 1);
    assert!(malformed.stats.has_usable_drawing_data());
    assert!(malformed.stats.recovered());
    assert_eq!(malformed.stats.skipped_source_records, 1);

    let empty = fs::read(Path::new(&format!("{prefix}-empty.dxf"))).unwrap();
    let empty_result = DxfReader::from_reader(std::io::Cursor::new(empty))
        .unwrap()
        .recover();
    assert!(matches!(empty_result, Err(acadrust::DxfError::RecoveryFailed(_))));
}
