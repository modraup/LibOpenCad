// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (`unused_app_ids`).
use acadrust::DxfReader;

fn unused_names(doc: &acadrust::CadDocument) -> Vec<String> {
    doc.unused_app_ids()
        .into_iter()
        .map(|handle| {
            doc.app_ids
                .iter()
                .find(|app| app.handle == handle)
                .expect("APPID")
                .name
                .to_uppercase()
        })
        .collect()
}

fn compare(label: &str, path: &str) {
    let doc = DxfReader::from_file(path).unwrap().read().unwrap();
    assert_eq!(unused_names(&doc), vec!["IDLE".to_string()], "{label}");
}

fn main() {
    let prefix = std::env::args().nth(1).expect("prefix");
    for label in ["ascii", "binary", "ascii-ac1015"] {
        compare(label, &format!("{prefix}-{label}-roundtrip.dxf"));
        compare(label, &format!("{prefix}-{label}-source.dxf"));
    }
}
