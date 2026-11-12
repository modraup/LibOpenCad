// SPDX-License-Identifier: MPL-2.0
use acadrust::{AuditIssueCode, DxfReader, Handle};
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(format!("{prefix}.dxf"))?.read()?;
    let cpp = std::fs::read_to_string(format!("{prefix}.txt"))?;
    assert!(cpp.lines().count() >= 12);
    for line in cpp.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        let h = Handle::new(u64::from_str_radix(fields[1], 16).unwrap());
        let expected = u64::from_str_radix(fields[2], 16).unwrap();
        let owner = if let Some(entity) = doc.get_entity(h) {
            entity.common().owner_handle
        } else if let Some(owner) = doc.object_owner(h) {
            owner
        } else if doc.block_records.iter().any(|b| b.handle == h) {
            doc.block_records.handle()
        } else if doc.layers.iter().any(|b| b.handle == h) {
            doc.layers.handle()
        } else {
            assert!(h == doc.layers.handle() || h == doc.block_records.handle());
            Handle::NULL
        };
        assert_eq!(owner.value(), expected, "{line}");
    }
    let report = doc.audit();
    for (code, handle) in [
        (AuditIssueCode::DanglingReference, 0xdead),
        (AuditIssueCode::OwnerCycle, 0x1003),
        (AuditIssueCode::OwnerCycle, 0x1005),
        (AuditIssueCode::DuplicateHandle, 0x31),
    ] {
        assert!(
            report
                .issues
                .iter()
                .any(|i| i.code == code && i.handle == Some(Handle::new(handle))),
            "missing Rust diagnostic {code:?} for {handle:X}"
        );
    }
    Ok(())
}
