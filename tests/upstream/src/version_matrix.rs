// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType, Vector3};
use std::io::Cursor;
fn version(name: &str) -> DxfVersion {
    match name {
        "AC1009" => DxfVersion::AC1009,
        "AC1012" => DxfVersion::AC1012,
        "AC1014" => DxfVersion::AC1014,
        "AC1015" => DxfVersion::AC1015,
        "AC1018" => DxfVersion::AC1018,
        "AC1021" => DxfVersion::AC1021,
        "AC1024" => DxfVersion::AC1024,
        "AC1027" => DxfVersion::AC1027,
        "AC1032" => DxfVersion::AC1032,
        _ => panic!("version"),
    }
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let versions = [
        "AC1009", "AC1012", "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032",
    ];
    for src in versions {
        for input in ["a", "b"] {
            for dst in versions {
                for output in ["a", "b"] {
                    if dst == "AC1009" && output == "b" {
                        continue;
                    }
                    let path = format!("{prefix}-cross-{src}-{input}-{dst}-{output}.dxf");
                    let doc = DxfReader::from_file(&path)?.read()?;
                    assert_eq!(doc.version, version(dst), "{path}");
                    assert_eq!(doc.entities().count(), 3);
                    assert!(doc.entities().any(|e|matches!(e,EntityType::Line(l) if l.start==Vector3::new(1.,2.,3.) && l.end==Vector3::new(4.,5.,6.))));
                    assert!(doc.entities().any(|e|matches!(e,EntityType::Circle(c) if c.radius==10. && c.center==Vector3::new(7.,8.,9.))));
                    assert!(doc.entities().any(|e|matches!(e,EntityType::Text(t) if t.value=="中文版本测试" && t.height==2.5)));
                }
            }
        }
    }
    let plans = std::fs::read_to_string(format!("{prefix}-plans.txt"))?;
    let mut count = 0;
    for line in plans.lines() {
        let columns: Vec<_> = line.split_whitespace().collect();
        let source = version(columns[0]);
        let binary = columns[1] == "1";
        let explicit = columns[2] == "1";
        let mut doc = CadDocument::new();
        doc.version = source;
        let writer = if binary {
            DxfWriter::new_binary(&doc)
        } else {
            DxfWriter::new(&doc)
        };
        let result = if explicit {
            writer.with_version(source).write_to_vec()
        } else {
            writer.write_to_vec()
        };
        if columns[3] == "REJECT" {
            assert!(result.is_err());
        } else {
            let output = DxfReader::from_reader(Cursor::new(result?))?.read()?;
            assert_eq!(output.version, version(columns[3]), "{line}");
        }
        if !explicit {
            let format = if binary { "binary" } else { "ascii" };
            let native =
                DxfReader::from_file(format!("{prefix}-{}-{format}.dxf", columns[0]))?.read()?;
            assert_eq!(native.version, source);
            assert_eq!(native.entities().count(), 3);
            for e in native.entities() {
                match e {
                    EntityType::Line(v) => {
                        assert_eq!(v.start, Vector3::new(1., 2., 3.));
                        assert_eq!(v.end, Vector3::new(4., 5., 6.));
                    }
                    EntityType::Circle(v) => {
                        assert_eq!(v.center, Vector3::new(7., 8., 9.));
                        assert_eq!(v.radius, 10.);
                    }
                    EntityType::Text(v) => assert_eq!(v.value, "中文版本测试"),
                    _ => panic!("unexpected entity"),
                }
            }
        }
        count += 1;
    }
    assert_eq!(count, 36);
    println!(
        "Rust verified 36 actual output-policy cases and 18 native version-preserving drawings"
    );
    Ok(())
}
pub fn diagnostics(prefix: &str) -> acadrust::Result<()> {
    for format in ["ascii", "binary"] {
        for page in ["empty", "UNKNOWN", "ANSI_1252"] {
            for file in ["source", "recovered"] {
                let doc =
                    DxfReader::from_file(format!("{prefix}-{format}-page-{page}-{file}.dxf"))?
                        .with_configuration(DxfReaderConfiguration {
                            default_encoding: Some("ANSI_936".into()),
                            ..Default::default()
                        })
                        .read()?;
                assert!(doc
                    .entities()
                    .any(|e| matches!(e,EntityType::Text(t) if t.value=="€中")));
            }
        }
        for header in ["no-header", "no-version"] {
            for encoding in ["auto", "ANSI_936", "UTF-8", "invalid"] {
                let base = format!("{prefix}-{format}-{header}-{encoding}");
                let expected = match encoding {
                    "invalid" if format == "ascii" => "aá\u{80}",
                    "invalid" => "a�",
                    _ => "中文",
                };
                for file in ["source", "recovered"] {
                    for failsafe in [false, true] {
                        let default_encoding = (file == "source"
                            && matches!(encoding, "ANSI_936" | "UTF-8"))
                        .then(|| encoding.to_string());
                        let doc = DxfReader::from_file(format!("{base}-{file}.dxf"))?
                            .with_configuration(DxfReaderConfiguration {
                                failsafe,
                                default_encoding,
                                ..Default::default()
                            })
                            .read()?;
                        assert_eq!(doc.version, DxfVersion::AC1032);
                        let texts: Vec<_> = doc
                            .entities()
                            .filter_map(|e| match e {
                                EntityType::Text(t) => {
                                    Some((t.common.handle.value(), t.value.as_str()))
                                }
                                _ => None,
                            })
                            .collect();
                        assert_eq!(texts, [(0xa1, expected)], "{base}-{file}");
                    }
                }
            }
        }
        for (case, expected) in [
            ("gbk-lead", "甲� 乙"),
            ("gbk-tail", "甲�"),
            ("gbk-pair", "甲�乙"),
            ("gbk-ff", "甲�乙"),
            ("cif-short", r"甲\U+12"),
            ("cif-hex", r"甲\U+12G4乙"),
            ("cif-high", "甲中乙"),
            ("cif-low", "甲乙"),
            ("valid", "甲乗U+12G4😀€😀乙"),
        ] {
            for file in ["source", "recovered"] {
                let path = format!("{prefix}-legacy-{format}-{case}-{file}.dxf");
                let doc = DxfReader::from_file(&path)?.read()?;
                assert_eq!(doc.version, DxfVersion::AC1015);
                let texts: std::collections::BTreeMap<_, _> = doc
                    .entities()
                    .map(|e| {
                        let EntityType::Text(t) = e else {
                            panic!("unexpected entity in {path}");
                        };
                        (e.common().handle.value(), t.value.as_str())
                    })
                    .collect();
                assert_eq!(
                    texts,
                    [(0xa1, "前邻中文"), (0xb2, expected), (0xc3, "后邻中文")].into(),
                    "{path}"
                );
            }
        }
    }
    for encoding in ["ascii", "binary"] {
        let doc = DxfReader::from_file(format!("{prefix}-fallback-{encoding}.dxf"))?.read()?;
        let text = doc
            .entities()
            .find_map(|e| {
                if let EntityType::Text(t) = e {
                    Some(t)
                } else {
                    None
                }
            })
            .unwrap();
        assert_eq!(text.value, "中文");
    }
    use acadrust::DxfReaderConfiguration;
    for format in ["ascii", "binary"] {
        for case in [
            "header-type",
            "header-empty",
            "unknown-version",
            "header-number",
            "unnamed-section",
            "section-next",
            "section-eof",
            "physical-end",
            "partial-value",
            "partial-marker",
            "poly-no-end",
            "insert-no-end",
            "wrong-child",
            "poly-cut",
        ] {
            let base = format!("{prefix}-{format}-{case}");
            let src = format!("{base}-source.dxf");
            let strict = DxfReader::from_file(&src)?.read().is_ok();
            let raw = DxfReader::from_file(&src)?
                .with_configuration(DxfReaderConfiguration {
                    failsafe: true,
                    ..Default::default()
                })
                .read_with_stats()?;
            let handles = |doc: &CadDocument| {
                let mut h: Vec<_> = doc.entities().map(|e| e.common().handle.value()).collect();
                h.sort();
                h
            };
            // Rust counts top-level decoded entities. The native reader counts
            // physical records and discards an incomplete compound as a unit.
            let (raw_handles, complete, skipped): (&[u64], bool, usize) = match case {
                "section-next" => (&[], true, 0),
                "section-eof" => (&[0xa1, 0xb2, 0x100], false, 0),
                "physical-end" => (&[0xa1], false, 0),
                "partial-value" => (&[0xa1], false, 1),
                "partial-marker" if format == "ascii" => (&[], false, 1),
                "partial-marker" => (&[0xa1, 0x100], false, 0),
                "poly-no-end" => (&[0xa1, 0xb2, 0xc1], true, 0),
                "insert-no-end" => (&[0xa1, 0xb2, 0xd1], true, 0),
                "wrong-child" => (&[0xa1, 0xb2, 0xc1, 0xd2], true, 0),
                "poly-cut" => (&[0xa1, 0xc1], false, 0),
                _ => (&[0xa1, 0xb2], true, 0),
            };
            assert_eq!(
                handles(&raw.document),
                raw_handles,
                "{base} Rust recovered handles"
            );
            assert_eq!(strict, complete, "{base} Rust strict outcome");
            assert_eq!(raw.stats.stream_completed, complete, "{base} Rust EOF");
            assert_eq!(
                raw.stats.decoded_source_records,
                raw_handles.len(),
                "{base} Rust decoded count"
            );
            assert_eq!(
                raw.stats.skipped_source_records, skipped,
                "{base} Rust skipped count"
            );
            if !complete {
                let eof = raw
                    .stats
                    .diagnostics
                    .last()
                    .expect("Rust truncation diagnostic");
                assert_eq!(eof.code, "stream-ended-early", "{base}");
                assert_eq!(
                    eof.source_offset,
                    Some(std::fs::metadata(&src)?.len()),
                    "{base} physical end"
                );
            }
            let clean = DxfReader::from_file(format!("{base}-recovered.dxf"))?.read()?;
            let expected = match case {
                "physical-end" | "partial-value" | "partial-marker" | "poly-cut" => vec![0xa1],
                _ => vec![0xa1, 0xb2],
            };
            assert_eq!(handles(&clean), expected, "{case} native recovered handles");
            for e in clean.entities() {
                match e {
                    EntityType::Line(l) => {
                        assert_eq!(l.start, Vector3::new(1., 2., 3.));
                        assert_eq!(l.end, Vector3::new(4., 5., 6.));
                    }
                    EntityType::Circle(c) => {
                        assert_eq!(c.center, Vector3::new(2., 3., 4.));
                        assert_eq!(c.radius, 5.);
                    }
                    _ => panic!("unexpected recovered geometry in {base}"),
                }
            }
        }
    }
    assert!(DxfReader::from_file(format!("{prefix}-truncated.dxf"))?
        .read()
        .is_err());
    let recovered = DxfReader::from_file(format!("{prefix}-truncated.dxf"))?
        .with_configuration(DxfReaderConfiguration {
            failsafe: true,
            ..Default::default()
        })
        .read_with_stats()?;
    assert!(!recovered.stats.stream_completed);
    assert!(!recovered.stats.diagnostics.is_empty());
    let partial = DxfReader::from_file(format!("{prefix}-partial.dxf"))?.read()?;
    assert_eq!(partial.entities().count(), 1);
    let cleaned = DxfReader::from_file(format!("{prefix}-recovered.dxf"))?.read()?;
    assert_eq!(cleaned.entities().count(), 1);
    if let EntityType::Circle(c) = cleaned.entities().next().unwrap() {
        assert_eq!(c.radius, 5.);
    } else {
        panic!("neighbor not preserved");
    }
    // Rust's numeric parsing may default malformed values; C++ diagnostic strict
    // mode explicitly rejects them. Do not claim identical recovery counters.
    println!("Rust verified the 28 structural recovery fixtures and the original and recovered text of 18 legacy encoding fixtures");
    Ok(())
}
