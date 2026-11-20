// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfVersion, DxfWriter, EntityType};
use std::io::Cursor;
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(format!("{prefix}-source.dxf"))?.read()?;
    for v in [
        DxfVersion::AC1012,
        DxfVersion::AC1014,
        DxfVersion::AC1015,
        DxfVersion::AC1018,
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let actual = DxfReader::from_file(format!("{prefix}-{}.dxf", v.to_dxf_string()))?.read()?;
        let reference = DxfReader::from_reader(Cursor::new(
            DxfWriter::new(&source).with_version(v).write_to_vec()?,
        ))?
        .read()?;
        assert_eq!(actual.version, v);
        for e in actual.entities() {
            assert_eq!(Some(e), reference.get_entity(e.common().handle));
        }
        assert!(actual.entities().any(|e| matches!(e, EntityType::Hatch(_))));
    }
    Ok(())
}
