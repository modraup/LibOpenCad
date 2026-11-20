// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, Handle};
pub fn verify(input: &str, output: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let result = DxfReader::from_file(output)?.read()?;
    let text = std::fs::read_to_string(format!("{output}.parts"))?;
    let mut seen = std::collections::BTreeSet::new();
    let mut rows = 0;
    let mut part_count = 0;
    for row in text.lines() {
        let words: Vec<_> = row.split_whitespace().collect();
        assert!(words.len() >= 2);
        let handle = Handle::new(u64::from_str_radix(words[0], 16).unwrap());
        assert!(seen.insert(handle));
        let count: usize = words[1].parse().unwrap();
        assert_eq!(words.len(), count + 2);
        let entity = source.get_entity(handle).unwrap();
        assert_ne!(entity.common().layer, "Control");
        let expected = entity.explode();
        assert_eq!(expected.len(), count);
        for (mut expected, h) in expected.into_iter().zip(&words[2..]) {
            let h = Handle::new(u64::from_str_radix(h, 16).unwrap());
            assert!(seen.insert(h));
            assert!(source.get_entity(h).is_none());
            let actual = result.get_entity(h).unwrap();
            assert!(result
                .block_records
                .iter()
                .any(|b| b.name == "*Paper_Space" && b.handle == actual.common().owner_handle));
            let common = expected.common_mut();
            common.handle = h;
            common.owner_handle = actual.common().owner_handle;
            common.entity_mode = actual.common().entity_mode;
            crate::entity_transform::compare(actual, &expected);
            part_count += 1;
        }
        rows += 1;
    }
    assert_eq!(rows, 249);
    assert!(part_count > 0);
    assert_eq!(
        result.entities().count(),
        source.entities().count() + part_count
    );
    for e in source.entities() {
        assert_eq!(result.get_entity(e.common().handle).unwrap(), e);
    }
    assert_eq!(source.layers, result.layers);
    assert_eq!(source.objects, result.objects);
    println!("Verified {rows} source entities and {part_count} detached primitive results");
    Ok(())
}
