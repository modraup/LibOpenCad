// SPDX-License-Identifier: MPL-2.0
use acadrust::audit::AuditIssueCode;
use acadrust::{DxfReader, EntityType};
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for name in [
        "clean", "null", "seed", "owner", "invalid", "reactor", "cycle",
    ] {
        let d = DxfReader::from_file(format!("{prefix}.{name}.dxf"))?.read()?;
        let report = d.audit();
        if name == "invalid" {
            assert_eq!(report.count(AuditIssueCode::InvalidGeometry), 2);
            assert_eq!(report.count(AuditIssueCode::DanglingReference), 1);
            assert_eq!(report.issues.len(), 3);
        } else if name == "cycle" {
            assert_eq!(report.count(AuditIssueCode::OwnerCycle), 2);
        } else if name == "reactor" {
            assert_eq!(report.warning_count(), 1);
            assert_eq!(report.error_count(), 0);
        } else {
            assert!(report.is_clean(), "{name}: {:?}", report.issues);
        }
        assert_eq!(d.entities().count(), 3);
        let line = d
            .entities()
            .find_map(|e| {
                if let EntityType::Line(v) = e {
                    Some(v)
                } else {
                    None
                }
            })
            .unwrap();
        assert_eq!(line.start, acadrust::Vector3::new(1., 2., 3.));
        assert_eq!(line.end, acadrust::Vector3::new(4., 5., 6.));
        let mut repaired = d.clone();
        let r = repaired.repair();
        assert_eq!(r.after.issues.len(), report.issues.len());
    }
    Ok(())
}
