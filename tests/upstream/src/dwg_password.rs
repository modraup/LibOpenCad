// SPDX-License-Identifier: MPL-2.0
use acadrust::io::dwg::DwgReadOptions;
use acadrust::{DwgReader, DxfError, EntityType, Vector3};

pub fn verify(version: &str, fixture: &str) -> acadrust::Result<()> {
    let password = if version == "AC1021" {
        "Straßeı😀CaseMixCaseMixCaseMixCaseMixCaseMixCaseMix"
    } else {
        "budweiser"
    };
    for (attempt, expected) in [
        (None, "please provide a password"),
        (Some("incorrect"), "Invalid password"),
        (Some(""), "Invalid password"),
    ] {
        let mut options = DwgReadOptions::default();
        options.password = attempt.map(str::to_owned);
        let error = DwgReader::from_file_with_options(fixture, options.clone())?
            .read_with_stats()
            .unwrap_err();
        assert!(matches!(&error, DxfError::Decryption(text) if text.contains(expected)));
        options.failsafe = true;
        let partial = DwgReader::from_file_with_options(fixture, options)?.read_with_stats()?;
        assert!(!partial.stats.has_usable_drawing_data());
        assert!(partial.stats.diagnostics.iter().any(|diagnostic| {
            diagnostic.code == "decryption-failed"
                && diagnostic.section.as_deref() == Some("AcDb:Security")
                && diagnostic.message.contains(expected)
        }));
    }

    let read = DwgReader::from_file(fixture)?
        .with_password(password)
        .read_with_stats()?;
    assert!(read.stats.has_usable_drawing_data() && read.stats.stream_completed);
    assert_eq!(read.stats.source_version, version);
    assert_eq!(read.stats.skipped_source_records, 0);
    if version == "AC1032" {
        let modelers: Vec<_> = read
            .document
            .entities()
            .filter_map(|entity| match entity {
                EntityType::Solid3D(value) => Some(&value.acis_data),
                EntityType::Region(value) => Some(&value.acis_data),
                EntityType::Body(value) => Some(&value.acis_data),
                _ => None,
            })
            .collect();
        assert_eq!(modelers.len(), 9);
        assert!(modelers.iter().any(|data| !data.sab_data.is_empty()));
    } else {
        let mut starts: Vec<_> = read
            .document
            .entities()
            .filter_map(|entity| match entity {
                EntityType::Line(value) => Some(value.start),
                _ => None,
            })
            .collect();
        starts.sort_by(|a, b| a.x.total_cmp(&b.x));
        assert_eq!(
            starts,
            [
                Vector3::new(1., 2., 3.),
                Vector3::new(5., 6., 7.),
                Vector3::new(9., 10., 11.)
            ]
        );
    }
    Ok(())
}
