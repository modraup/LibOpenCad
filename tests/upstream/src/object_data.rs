// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::EntityType;
use acadrust::objects::{ObjectDataField, ObjectDataTable, ObjectDataType, ObjectDataValue};
use acadrust::types::{DxfVersion, Handle};
use acadrust::{DwgReader, DxfReader};

fn table() -> ObjectDataTable {
    ObjectDataTable::new(
        "TEST",
        vec![
            ObjectDataField::new("VOLTAGE", ObjectDataType::Real),
            ObjectDataField::new("CIRCUITS", ObjectDataType::Integer),
            ObjectDataField::new("OWNER", ObjectDataType::Character),
        ],
    )
}

fn values(owner: &str) -> Vec<ObjectDataValue> {
    vec![
        ObjectDataValue::Real(230.0),
        ObjectDataValue::Integer(2),
        ObjectDataValue::Character(owner.into()),
    ]
}

fn dxf(path: &str) -> acadrust::CadDocument {
    DxfReader::from_file(path)
        .unwrap_or_else(|e| panic!("{path}: {e}"))
        .read()
        .unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn dwg(path: &str) -> acadrust::CadDocument {
    DwgReader::from_file(path)
        .unwrap_or_else(|e| panic!("{path}: {e}"))
        .read()
        .unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn line(document: &acadrust::CadDocument) -> Handle {
    document
        .entities()
        .find(|entity| matches!(entity, EntityType::Line(_)))
        .unwrap()
        .common()
        .handle
}

fn check_hello(document: &acadrust::CadDocument, tables: usize) {
    let read = document.object_data_tables();
    assert_eq!(read.len(), tables, "object data tables");
    assert!(
        read.iter().any(|(_, item)| item == &table()),
        "TEST table"
    );
    let records = document.object_data(line(document));
    assert!(
        records
            .iter()
            .any(|record| record.values == values("hello") || record.values == values("x")),
        "object data values"
    );
}

pub fn verify(out: &str) -> acadrust::Result<()> {
    for path in [
        format!("{out}-ascii.dxf"),
        format!("{out}-binary.dxf"),
    ] {
        let doc = dxf(&path);
        assert_eq!(doc.version, DxfVersion::AC1032);
        check_hello(&doc, 2);
        let root = doc.named_objects_dictionary().expect("NOD");
        assert!(root.get("IrdDscDictionary").is_some(), "{path} NOD key");
    }

    let long = dxf(&format!("{out}-long-ascii.dxf"));
    let owner = "Hé,".to_owned() + &"l".repeat(351) + "o\"'";
    let records = long.object_data(line(&long));
    assert_eq!(records.len(), 1);
    assert_eq!(records[0].values, values(&owner));

    for version in [
        DxfVersion::AC1018,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        for format in ["ascii", "binary"] {
            let path = format!("{out}-{version:?}-{format}.dxf");
            let doc = dxf(&path);
            assert_eq!(doc.version, version, "{path}");
            check_hello(&doc, 2);
        }
    }

    let restored = dwg(&format!("{out}.dwg"));
    assert_eq!(restored.version, DxfVersion::AC1032);
    check_hello(&restored, 2);
    Ok(())
}
