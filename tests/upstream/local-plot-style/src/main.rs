// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 plot-style TTBVER1).
use acadrust::plot_style::PlotStyleTable;
use std::env;
use std::fs;
use std::path::Path;

fn verify_flags(prefix: &str) {
    use acadrust::{DxfReader, DxfVersion, EntityType};
    let verify = |suffix: &str, expected: u8, version: DxfVersion| {
        let doc = DxfReader::from_file(format!("{prefix}{suffix}.dxf"))
            .unwrap()
            .read()
            .unwrap();
        assert_eq!(doc.version, version);
        assert_eq!(doc.entities().count(), 1);
        let entity = doc.entities().next().unwrap();
        assert!(matches!(entity, EntityType::Line(_)));
        assert_eq!(entity.common().plotstyle_flags, expected);
        assert_eq!(entity.common().plotstyle_handle, None);
    };
    for (name, flag) in [("byblock", 1), ("named", 2)] {
        for format in ["ascii", "binary"] {
            for roundtrip in ["", "-roundtrip"] {
                verify(&format!("-{name}-{format}{roundtrip}"), flag, DxfVersion::AC1032);
            }
        }
    }
    for roundtrip in ["", "-roundtrip"] {
        verify(&format!("-ascii-ac1015{roundtrip}"), 1, DxfVersion::AC1015);
    }
}

fn main() {
    let mut args = env::args().skip(1);
    let prefix = args.next().expect("output prefix or flags mode");
    if prefix == "flags" {
        verify_flags(&args.next().expect("flags output prefix"));
        return;
    }
    let scaled = PlotStyleTable::from_bytes(
        "scaled.ctb",
        &fs::read(Path::new(&format!("{prefix}-scaled.ctb"))).unwrap(),
    )
    .expect("scaled.ctb");
    assert_eq!(scaled.scale_factor, 0.25);
    assert_eq!(scaled.resolve_lineweight(1), Some(0.125));

    let named = PlotStyleTable::from_bytes(
        "named.stb",
        &fs::read(Path::new(&format!("{prefix}-named.stb"))).unwrap(),
    )
    .expect("named.stb");
    assert!(named.is_stb);
    let black = named.named_entries.get("Black").expect("Black style");
    assert_eq!(black.color, Some([0, 0, 0]));
    let written = named.to_bytes().expect("write stb");
    assert_eq!(&written[19..26], b"STBVER1");

    let ttb_bytes = fs::read(Path::new(&format!("{prefix}-ttb.stb"))).unwrap();
    assert_eq!(&ttb_bytes[19..26], b"TTBVER1");
    let ttb = PlotStyleTable::from_bytes("named.stb", &ttb_bytes).expect("TTBVER1");
    assert!(ttb.is_stb);
    assert_eq!(
        ttb.named_entries.get("Black").expect("TTB Black").color,
        Some([0, 0, 0])
    );
}
