// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (MTEXT inline-height scaling).
use acadrust::entities::Entity;
use acadrust::types::Transform;
use acadrust::{DxfReader, EntityType};

fn texts(doc: &acadrust::CadDocument) -> (Vec<String>, Vec<String>) {
    let mut mtext = Vec::new();
    let mut leaders = Vec::new();
    for entity in doc.entities() {
        match entity {
            EntityType::MText(t) => mtext.push(t.value.clone()),
            EntityType::MultiLeader(t) => leaders.push(t.context.text_string.clone()),
            _ => {}
        }
    }
    (mtext, leaders)
}

fn read_dxf(path: &str) -> acadrust::CadDocument {
    DxfReader::from_file(path).unwrap().read().unwrap()
}

fn compare(label: &str, source_path: &str, scaled_path: &str) {
    let mut expected = read_dxf(source_path);
    let native = read_dxf(scaled_path);
    let transform = Transform::from_scale(2.0);
    for entity in expected.entities_mut() {
        entity.apply_transform(&transform);
    }
    let (want_mtext, want_leaders) = texts(&expected);
    let (got_mtext, got_leaders) = texts(&native);
    assert_eq!(want_mtext, got_mtext, "{label} MTEXT");
    assert_eq!(want_leaders, got_leaders, "{label} MULTILEADER");
    assert_eq!(
        want_mtext,
        vec![r#"A\H4;B\H2x;C\H2X;D\\H2;E\H5;F\H40;G\P中\H6;"#.to_string()],
        "{label} expected scaled contents"
    );
}

fn main() {
    let prefix = std::env::args().nth(1).expect("prefix");
    for label in ["ascii", "binary", "ascii-ac1018"] {
        compare(
            label,
            &format!("{prefix}-{label}-source.dxf"),
            &format!("{prefix}-{label}-scaled.dxf"),
        );
    }
}
