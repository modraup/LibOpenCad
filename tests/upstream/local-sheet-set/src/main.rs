// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 sheet sets).
use acadrust::sheet_set::{encode, SheetSetDatabase};
use acadrust::DxfReader;
use std::env;
use std::fs;
use std::path::Path;

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");
    assert_eq!(encode(b"<?xml"), vec![0xD0, 0xCD, 0x14, 0xE3, 0xE0]);

    let bytes = fs::read(Path::new(&format!("{prefix}-demo.dst"))).unwrap();
    let db = SheetSetDatabase::from_bytes(&bytes).expect("demo.dst");
    let sheet = db
        .sheet_for("c:/p/SHEET1.dwg", Some("Layout1"))
        .expect("sheet_for");
    assert_eq!(
        db.sheet_value(sheet, "Sheet", "NumberAndTitle").unwrap(),
        "A-101 - Ground Floor Plan"
    );
    assert_eq!(
        db.sheet_value(sheet, "Subset", "Name").unwrap(),
        "Architectural"
    );
    assert_eq!(db.sheet_value(sheet, "Sheet", "Drawn By").unwrap(), "RS");
    assert_eq!(
        db.sheet_value(sheet, "SheetSet", "Description").unwrap(),
        "A & B <c>"
    );
    let names: Vec<_> = sheet.children.iter().filter_map(|c| c.propname()).collect();
    assert_eq!(names, ["Desc", "Layout", "Number", "SheetViews", "Title"]);

    let drawing = DxfReader::from_file(Path::new(&format!("{prefix}-linked.dxf")))
        .unwrap()
        .read()
        .unwrap();
    let data = drawing.sheet_set_data().expect("sheet set data");
    assert_eq!(data.layout_name, "Layout1");
    assert_eq!(data.sheet_dwg_name, "sheet1.dwg");
    assert_eq!(data.sheet_set_version, 2);
    assert_eq!(data.update_count, 3);
    assert_eq!(data.update_time, "2026-11-19");
}
