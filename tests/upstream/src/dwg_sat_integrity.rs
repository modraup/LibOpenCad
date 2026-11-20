// SPDX-License-Identifier: MPL-2.0
use acadrust::{DwgReader, EntityType, Handle};
use std::fs;
pub fn verify(mode: &str, prefix: &str) -> acadrust::Result<()> {
    let versions: &[&str] = match mode {
        "framing" => &["AC1012", "AC1014", "AC1015"],
        "encoding" => &["AC1018", "AC1021", "AC1024", "AC1027", "AC1032"],
        _ => panic!("Unknown SAT integrity suite"),
    };
    for version in versions {
        let ids: Vec<u64> = fs::read_to_string(format!("{prefix}-{version}.ids"))?
            .split_whitespace()
            .map(|s| s.parse().unwrap())
            .collect();
        let d = DwgReader::from_file(format!("{prefix}-{version}.dwg"))?.read()?;
        let Some(EntityType::Solid3D(s)) = d.get_entity(Handle::new(ids[0])) else {
            panic!("Missing solid")
        };
        let body = s.acis_data.parse().unwrap();
        assert_eq!(body.records.len(), 87);
        assert_eq!(body.records.last().unwrap().entity_type, "point");
        let label = &body.records[85];
        assert_eq!(label.entity_type, "string-attrib");
        assert_eq!(
            label.tokens[0].as_string(),
            Some(if mode == "framing" {
                "review ASCII label"
            } else {
                "中文名称"
            })
        );
        assert_eq!(d.entities().count(), ids.len());
        if ids.len() == 2 {
            let Some(EntityType::Solid3D(other)) = d.get_entity(Handle::new(ids[1])) else {
                panic!("Lost neighbor")
            };
            let mut expected = acadrust::entities::acis::primitives::build_sphere([0., 0., 0.], 7.);
            expected.header.version = acadrust::entities::acis::SatVersion::V4_0;
            let expected =
                acadrust::entities::acis::SatDocument::parse(&expected.to_sat_string()).unwrap();
            crate::acis::compare(
                &crate::acis::canonical(&other.acis_data.parse().unwrap()),
                &crate::acis::canonical(&expected),
            );
        }
    }
    // Counted-string delimiter/newline defects shared by Rust are verified by
    // independent native expectations, not by requiring Rust's incorrect result.
    println!("{mode}: DWG SAT/SAB output verified by Rust");
    Ok(())
}
