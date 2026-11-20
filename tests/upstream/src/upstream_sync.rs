// SPDX-License-Identifier: MPL-2.0
use acadrust::compound_file::{
    BinaryRecord, CompoundEntry, CompoundFile, CompoundStorage, CompoundStream,
    CompoundStreamContent,
};
use acadrust::entities::hatch::HatchPatternLine;
use acadrust::entities::{Hatch, Ole2Frame};
use acadrust::{AuditIssueCode, DxfReader, EntityType, Handle, Vector2, Vector3};

// Same regression as upstream source_ole_data_is_written_back_verbatim: valid
// CFB with noncanonical sector slack, without a DWG geometry envelope.
pub fn generate(path: &str) -> acadrust::Result<()> {
    let stream: Vec<u8> = (0..9000u32)
        .map(|i| (i.wrapping_mul(2_654_435_761).wrapping_add(2) >> 13) as u8)
        .collect();
    let mut root = CompoundStorage::root();
    root.entries.push(CompoundEntry::Stream(CompoundStream {
        name: "CONTENTS".into(),
        class_id: [0; 16],
        state_bits: 0,
        creation_time: 0,
        modified_time: 0,
        content: CompoundStreamContent::Binary(BinaryRecord::split(&stream, 4096)),
    }));
    let mut frame = Ole2Frame::new();
    frame.storage.compound_file = Some(CompoundFile {
        root,
        ..CompoundFile::default()
    });
    let mut raw = frame.encoded_payload();
    let end = raw.windows(64).position(|w| w == &stream[..64]).unwrap() + stream.len();
    let slack = (512 - end % 512) % 512;
    assert!(slack > 0);
    raw[end..end + slack].fill(0xAB);
    let mut decoded = Ole2Frame::new();
    decoded.set_payload(&raw);
    assert_eq!(decoded.encoded_payload(), raw);
    decoded.source_storage = Default::default();
    assert_ne!(decoded.encoded_payload(), raw);
    std::fs::write(path, raw)?;
    Ok(())
}
fn pattern() -> Hatch {
    let mut h = Hatch::new();
    h.pattern.lines = vec![
        HatchPatternLine {
            angle: 0.2,
            base_point: Vector2::new(3., 5.),
            offset: Vector2::new(0., 10.),
            dash_lengths: vec![2., -1., 0.],
        },
        HatchPatternLine {
            angle: 1.1,
            base_point: Vector2::new(-2., 8.),
            offset: Vector2::new(4., 3.),
            dash_lengths: vec![],
        },
    ];
    h
}
fn near(a: f64, b: f64) {
    assert!(
        (a - b).abs() <= 1e-10 * (1. + a.abs().max(b.abs())),
        "{a} != {b}"
    );
}
pub fn verify(prefix: &str, payload: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(format!("{prefix}.dxf"))?.read()?;
    let raw = std::fs::read(payload)?;
    let mut expected = Vec::new();
    let mut h = pattern();
    h.set_pattern_scale(0.2);
    h.set_pattern_angle(std::f64::consts::FRAC_PI_2);
    h.set_pattern_scale(0.2);
    h.set_pattern_angle(std::f64::consts::FRAC_PI_2);
    expected.push(h.clone());
    h.set_pattern_angle(-0.35);
    h.set_pattern_scale(-2.);
    expected.push(h.clone());
    h.pattern.update(Vector2::new(7., -3.), 0.4, 1.5);
    expected.push(h);
    h = pattern();
    h.pattern_scale = 0.;
    h.set_pattern_scale(3.);
    expected.push(h);
    h = pattern();
    h.pattern_scale = f64::EPSILON;
    h.set_pattern_scale(0.5);
    expected.push(h);
    let mut hi = 0;
    let mut oi = 0;
    let mut li = 0;
    for e in doc.entities() {
        match e {
            EntityType::Hatch(got) => {
                let want = &expected[hi];
                hi += 1;
                near(got.pattern_angle, want.pattern_angle);
                near(got.pattern_scale, want.pattern_scale);
                assert_eq!(got.pattern.lines.len(), want.pattern.lines.len());
                for (a, b) in got.pattern.lines.iter().zip(&want.pattern.lines) {
                    for (x, y) in [
                        (a.angle, b.angle),
                        (a.base_point.x, b.base_point.x),
                        (a.base_point.y, b.base_point.y),
                        (a.offset.x, b.offset.x),
                        (a.offset.y, b.offset.y),
                    ] {
                        near(x, y);
                    }
                    assert_eq!(a.dash_lengths.len(), b.dash_lengths.len());
                    for (&x, &y) in a.dash_lengths.iter().zip(&b.dash_lengths) {
                        near(x, y);
                    }
                }
            }
            EntityType::Ole2Frame(o) => {
                assert_eq!(o.encoded_payload(), raw);
                assert!(!o.rewrites_source_storage());
                let dx = if oi == 0 { 0. } else { 5. };
                assert_eq!(o.upper_left_corner, Vector3::new(10. + dx, 40., 0.));
                assert_eq!(o.lower_right_corner, Vector3::new(70. + dx, 5., 0.));
                let members: Vec<_> = if oi == 0 {
                    doc.model_space_entities().collect()
                } else {
                    doc.entities_in_block("*Paper_Space").collect()
                };
                assert!(members.iter().any(|e| e.common().handle == o.common.handle));
                oi += 1;
            }
            EntityType::Line(l) => {
                assert_eq!(
                    l.end,
                    if li == 0 {
                        Vector3::new(25., 0., 0.)
                    } else {
                        Vector3::new(10., 5., 0.)
                    }
                );
                li += 1;
            }
            _ => panic!("unexpected sync entity"),
        }
    }
    assert_eq!((hi, oi, li), (5, 2, 2));
    assert!(doc.audit().is_clean(), "{:?}", doc.audit());
    let owners = DxfReader::from_file(format!("{prefix}-owners.dxf"))?.read()?;
    let report = owners.audit();
    for id in [0xF001, 0xF002] {
        assert!(report
            .issues
            .iter()
            .any(|i| i.code == AuditIssueCode::OwnerMismatch && i.handle == Some(Handle::new(id))));
    }
    assert!(
        report
            .issues
            .iter()
            .filter(|i| i.code == AuditIssueCode::OwnerMismatch)
            .count()
            >= 3
    );
    let mut before = DxfReader::from_file(format!("{prefix}-flags-before.dxf"))?.read()?;
    let after = DxfReader::from_file(format!("{prefix}-flags-after.dxf"))?.read()?;
    assert!(
        before
            .block_records
            .get("Empty")
            .unwrap()
            .flags
            .has_attributes
    );
    assert!(
        !after
            .block_records
            .get("Empty")
            .unwrap()
            .flags
            .has_attributes
    );
    assert!(
        after
            .block_records
            .get("WithAttribute")
            .unwrap()
            .flags
            .has_attributes
    );
    before.repair();
    assert_eq!(before.block_records, after.block_records);
    assert!(after.audit().is_clean(), "{:?}", after.audit());
    Ok(())
}
