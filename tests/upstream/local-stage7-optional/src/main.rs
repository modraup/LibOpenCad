// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 nested copy, count, fields, CFB).
use acadrust::compound_file::StructuredStoragePayload;
use acadrust::count::{block_instances, count_of, evaluate, CountKey};
use acadrust::entities::EntityType;
use acadrust::fields::{self, FieldContext};
use acadrust::DxfReader;
use std::env;
use std::fs;
use std::path::Path;

struct TestContext;

impl FieldContext for TestContext {
    fn now_julian(&self) -> f64 {
        2_460_000.5
    }
    fn login(&self) -> Option<String> {
        Some("builder".into())
    }
}

fn read_dxf(path: &Path) -> acadrust::CadDocument {
    DxfReader::from_file(path).unwrap().read().unwrap()
}

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");
    let drawing = read_dxf(Path::new(&format!("{prefix}-optional.dxf")));
    assert!(drawing.layers.get("Detail").is_some());
    assert!(drawing.line_types.get("Dash").is_some());
    let line = drawing
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::Line(line) => Some(line),
            _ => None,
        })
        .expect("localized line");
    assert_eq!(line.common.layer, "Detail");
    assert_eq!(line.common.linetype, "Dash");

    let instances = block_instances(&drawing, None);
    assert_eq!(instances.len(), 3);
    assert_eq!(count_of(&instances, "Bolt", &CountKey::default()), 2);
    assert_eq!(
        evaluate(&drawing, r#"\AcCount {"name":"Bolt","type":"block"}"#),
        "2"
    );

    let fields = read_dxf(Path::new(&format!("{prefix}-fields.dxf")));
    let host = fields
        .model_space_entities()
        .find_map(|entity| match entity {
            EntityType::MText(text) => Some(text.common.handle),
            _ => None,
        })
        .expect("field host");
    assert_eq!(
        fields::resolve(&fields, host, &TestContext).as_deref(),
        Some("3")
    );

    let cfb = fs::read(Path::new(&format!("{prefix}-empty.cfb"))).unwrap();
    let payload = StructuredStoragePayload::decode(&cfb);
    let compound = payload.compound_file.expect("compound file");
    assert_eq!(compound.root.name, "Root Entry");
}
