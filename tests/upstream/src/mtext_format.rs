// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::mtext_format::{parse_mtext, parse_plain_text, MTextDocument};
fn unhex(s: &str) -> String {
    String::from_utf8(
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
            .collect(),
    )
    .unwrap()
}
pub fn verify(input: &str, output: &str) -> acadrust::Result<()> {
    let input = std::fs::read_to_string(input)?;
    let output = std::fs::read_to_string(output)?;
    let mut lines = output.lines();
    let mut count = 0;
    for encoded in input.lines() {
        let input = unhex(encoded);
        for mode in 0..3 {
            let d = if mode == 2 {
                parse_plain_text(&input)
            } else {
                parse_mtext(&input, mode == 1)
            };
            let values: Vec<_> = lines
                .next()
                .expect("missing native result")
                .split('\t')
                .collect();
            assert_eq!(values[0].parse::<usize>().unwrap(), mode);
            assert_eq!(
                values[1].parse::<usize>().unwrap(),
                d.paragraphs.len(),
                "paragraphs input={input:?} mode={mode}"
            );
            assert_eq!(
                values[2].parse::<usize>().unwrap(),
                d.span_count(),
                "spans input={input:?} mode={mode}"
            );
            assert_eq!(values[3] == "1", d.is_all_plain(), "plain input={input:?}");
            assert_eq!(unhex(values[4]), d.to_plain_text(), "text input={input:?}");
            // Compare serialized semantic structures, allowing numeric spelling differences.
            // Both serializers intentionally omit end_properties; compare those separately.
            assert_eq!(
                parse_mtext(&unhex(values[5]), true),
                parse_mtext(&d.to_mtext_string(), true),
                "serialized input={input:?} mode={mode}"
            );
            assert_eq!(values.len(), 6 + d.paragraphs.len());
            for (i, p) in d.paragraphs.iter().enumerate() {
                let mut end = MTextDocument::from_text("END");
                end.paragraphs[0].spans[0].properties = p.end_properties.clone();
                assert_eq!(
                    parse_mtext(&unhex(values[6 + i]), true),
                    parse_mtext(&end.to_mtext_string(), true),
                    "paragraph end input={input:?} mode={mode}"
                );
            }
            count += 1;
        }
    }
    assert!(lines.next().is_none());
    println!("Rust verified {count} MTEXT/TEXT parser and serializer cases");
    Ok(())
}
