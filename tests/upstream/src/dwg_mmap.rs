// SPDX-License-Identifier: MPL-2.0
use acadrust::{DwgReader, DxfError};

pub fn verify(kind: &str, fixture: &str) -> acadrust::Result<()> {
    let password = match kind {
        "linear" | "modeler" => None,
        "AC1021" => Some("Straßeı😀CaseMixCaseMixCaseMixCaseMixCaseMixCaseMix"),
        _ => Some("budweiser"),
    };
    let mut mapped = DwgReader::from_mmap(fixture)?;
    let mut streamed = DwgReader::from_file(fixture)?;
    if let Some(password) = password {
        mapped = mapped.with_password(password);
        streamed = streamed.with_password(password);
    }
    let mapped = mapped.read_with_stats()?;
    let streamed = streamed.read_with_stats()?;
    assert!(mapped.stats.has_usable_drawing_data() && mapped.stats.stream_completed);
    assert_eq!(mapped.stats, streamed.stats);
    assert!(mapped.document == streamed.document);
    if password.is_some() {
        for (attempt, expected) in [
            (None, "please provide a password"),
            (Some("incorrect"), "Invalid password"),
            (Some(""), "Invalid password"),
        ] {
            let mut reader = DwgReader::from_mmap(fixture)?;
            if let Some(attempt) = attempt {
                reader = reader.with_password(attempt);
            }
            let error = reader.read_with_stats().unwrap_err();
            assert!(matches!(error, DxfError::Decryption(message) if message.contains(expected)));
        }
    }
    Ok(())
}
