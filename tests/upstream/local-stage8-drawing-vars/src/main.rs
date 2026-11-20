// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, Transparency};
use std::env;
use std::path::Path;

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");
    let doc = DxfReader::from_file(Path::new(&format!("{prefix}-drawing-vars.dxf")))
        .unwrap()
        .read()
        .unwrap();
    assert_eq!(doc.drawing_variable("TEST"), Some("second"));
    assert_eq!(doc.hatch_origin(), [12.5, -8.25]);
    assert_eq!(
        doc.current_entity_transparency(),
        Transparency::Explicit(77)
    );
}
