// SPDX-License-Identifier: MPL-2.0
use acadrust::io::dwg::DwgReadOptions;
use acadrust::{DwgReader, EntityType};

pub fn verify(version: &str, prefix: &str) -> acadrust::Result<()> {
    let open = |name| DwgReader::from_file(format!("{prefix}-{name}.dwg"));
    let measured = open("valid")?.read_with_stats()?;
    let native_counts: Vec<usize> = std::fs::read_to_string(format!("{prefix}-stats.txt"))?
        .split_whitespace()
        .map(|value| value.parse().unwrap())
        .collect();
    let stats = &measured.stats;
    assert_eq!(stats.source_version, version);
    assert!(stats.stream_completed && stats.record_stream_read && !stats.recovery_mode);
    assert_eq!(stats.skipped_source_records, 0);
    assert_eq!(
        native_counts[..4],
        [
            stats.maintenance_version as usize,
            stats.source_sections,
            stats.source_records,
            stats.output_entities
        ]
    );
    assert!(native_counts[5] > 0 && stats.output_table_records > 0);
    if version == "AC1009" {
        // The pinned reader adds default objects and table entries to the legacy drawing.
        assert_eq!(native_counts[4], 0);
        assert!(stats.output_table_records >= native_counts[5]);
    } else {
        assert_eq!(
            native_counts[4..],
            [stats.output_objects, stats.output_table_records]
        );
    }
    let valid = open("valid")?.recover()?;
    assert!(valid.stats.has_usable_drawing_data());
    assert_eq!(
        valid
            .document
            .entities()
            .filter(|entity| matches!(entity, EntityType::Line(_)))
            .count(),
        3
    );
    assert!(valid.repair.after.scanned_entities >= 3);
    if version == "AC1009" {
        return Ok(());
    }

    assert!(open("damaged")?.read().is_err());
    assert!(open("damaged")?.read_with_stats().is_err());
    let measured_partial = DwgReader::from_file_with_options(
        format!("{prefix}-damaged.dwg"),
        DwgReadOptions::failsafe(),
    )?
    .read_with_stats()?;
    assert!(measured_partial.stats.recovery_mode && measured_partial.stats.stream_completed);
    assert_eq!(
        measured_partial.stats.output_entities + 1,
        measured.stats.output_entities
    );
    let partial = open("damaged")?.recover()?;
    assert!(partial.stats.has_usable_drawing_data());
    assert_eq!(partial.stats.skipped_source_records, 1);
    assert_eq!(
        measured_partial.stats.skipped_source_records,
        partial.stats.skipped_source_records
    );
    assert_eq!(
        measured_partial.stats.diagnostics,
        partial.stats.diagnostics
    );
    let diagnostic = partial
        .stats
        .diagnostics
        .iter()
        .find(|entry| entry.code == "record-catalog-failed")
        .unwrap();
    assert!(diagnostic.record_handle.is_some());
    assert_eq!(
        partial.document.entity_count() + 1,
        valid.document.entity_count()
    );

    let output = open("recovered")?.read_with_stats()?;
    assert_eq!(output.stats.skipped_source_records, 0);
    assert_eq!(
        output.document.entity_count(),
        partial.document.entity_count()
    );
    assert_eq!(
        output
            .document
            .model_space_entities()
            .filter(|entity| matches!(entity, EntityType::Line(_)))
            .count(),
        2
    );
    for entity in partial.document.entities() {
        let EntityType::Line(line) = entity else {
            continue;
        };
        let Some(EntityType::Line(reloaded)) = output.document.get_entity(line.common.handle)
        else {
            panic!("missing recovered LINE");
        };
        assert_eq!(line.start, reloaded.start);
        assert_eq!(line.end, reloaded.end);
        assert_eq!(line.common.owner_handle, reloaded.common.owner_handle);
    }
    Ok(())
}
