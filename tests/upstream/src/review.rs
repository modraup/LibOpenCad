// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, EntityType, Vector3};
use acadrust::objects::XRecordValue;

pub fn verify(prefix: &str, format: &str) -> acadrust::Result<()> {
    for scenario in ["names", "extension", "dictionary", "block"] {
        let doc = DxfReader::from_file(format!("{prefix}-{scenario}-{format}.dxf"))?.read()?;
        if scenario == "block" {
            assert_eq!(doc.entities().count(), 0);
            assert!(doc.block_records.get("ReviewBlock").is_none());
            continue;
        }
        assert_eq!(doc.entities().count(), 1);
        let EntityType::Line(line) = doc.entities().next().unwrap() else { panic!("expected line") };
        assert_eq!(line.start, Vector3::new(1., 2., 3.));
        assert_eq!(line.end, Vector3::new(4., 5., 6.));
        assert_eq!(doc.entities_in_block("*Model_Space").count(), 1);
        if scenario == "names" {
            assert_eq!(line.common.layer, "NewLayer");
            assert_eq!(line.common.linetype, "NewType");
        } else {
            let record = doc.xrecord(line.common.handle, "Review").expect("new extension entry");
            assert_eq!(record.get_first_by_code(1).unwrap().value, XRecordValue::String("preserved".into()));
        }
    }
    println!("Rust verified review regressions ({format})");
    Ok(())
}
