// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfWriter, Handle};
use std::fs;
fn compare_value(a: &impl std::fmt::Debug, b: &impl std::fmt::Debug) {
    let parts = |v: &str| {
        v.split(|c: char| c.is_whitespace() || "[]{}(),:".contains(c))
            .filter(|s| !s.is_empty())
            .map(str::to_owned)
            .collect::<Vec<_>>()
    };
    let aa = parts(&format!("{a:?}"));
    let bb = parts(&format!("{b:?}"));
    for (i, (x, y)) in aa.iter().zip(&bb).enumerate() {
        let equal = match (x.parse::<f64>(), y.parse::<f64>()) {
            (Ok(x), Ok(y)) => (x - y).abs() <= 1e-10 * x.abs().max(y.abs()).max(1.),
            _ => x == y,
        };
        assert!(
            equal,
            "Merge mismatch at token {i}: {:?} != {:?}",
            &aa[i.saturating_sub(5)..(i + 5).min(aa.len())],
            &bb[i.saturating_sub(5)..(i + 5).min(bb.len())]
        );
    }
    assert_eq!(aa.len(), bb.len(), "Merged value shape differs");
}
fn compare(d: &CadDocument, want: &CadDocument, entities: &[Handle], private: &[Handle]) {
    for &entity in entities {
        compare_value(
            &d.solid_history_graph(entity),
            &want.solid_history_graph(entity),
        );
        compare_value(
            &d.solid_history_tree(entity),
            &want.solid_history_tree(entity),
        );
        if let Some(g) = want.solid_history_graph(entity) {
            let mut handles = g.nodes.clone();
            handles.push(g.root);
            if let Some(e) = g.evaluation_graph {
                handles.push(e);
            }
            for h in handles {
                compare_value(&d.objects.get(&h), &want.objects.get(&h));
            }
        }
        // The tool entity and both geometric payloads survive a history merge.
        compare_value(
            d.get_entity(entity).unwrap(),
            want.get_entity(entity).unwrap(),
        );
    }
    for h in private {
        compare_value(&d.objects.get(h), &want.objects.get(h));
    }
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let handles = fs::read_to_string(format!("{prefix}.manifest"))?
        .split_whitespace()
        .map(|s| Handle::new(u64::from_str_radix(s, 16).unwrap()))
        .collect::<Vec<_>>();
    let (main, neighbor, surface) = (handles[0], handles[1], handles[2]);
    for binary in [false, true] {
        let ext = if binary { "binary" } else { "ascii" };
        let source = DxfReader::from_file(format!("{prefix}.prepared.{ext}.dxf"))?.read()?;
        for (case, a, b, op) in [
            ("union", main, neighbor, 0),
            ("intersect", neighbor, main, 1),
            ("subtract", main, neighbor, 2),
            ("surface-tool", main, surface, 2),
            ("surface-target", surface, main, 2),
            ("repeat", main, neighbor, 0),
            ("legacy", main, neighbor, 2),
            ("legacy-target", neighbor, main, 2),
        ] {
            eprintln!("Checking {case} {ext}");
            let mut expected = if case.starts_with("legacy") {
                DxfReader::from_file(format!("{prefix}.legacy-before.{ext}.dxf"))?.read()?
            } else {
                source.clone()
            };
            let retired = expected.solid_history_graph(b).unwrap();
            expected
                .merge_solid_history_boolean(a, b, op)
                .expect("Rust merge");
            if case == "repeat" {
                expected
                    .merge_solid_history_boolean(main, surface, 2)
                    .unwrap();
            }
            let oracle = format!("{prefix}.{case}.oracle.{ext}.dxf");
            (if binary {
                DxfWriter::new_binary(&expected)
            } else {
                DxfWriter::new(&expected)
            })
            .write_to_file(&oracle)?;
            let expected_read = DxfReader::from_file(oracle)?.read()?;
            let actual = DxfReader::from_file(format!("{prefix}.{case}.{ext}.dxf"))?.read()?;
            assert!(!actual.objects.contains_key(&retired.root));
            if let Some(h) = retired.evaluation_graph {
                assert!(!actual.objects.contains_key(&h));
            }
            assert!(actual.solid_history_graph(b).is_none());
            // Node is compared via its new history; the dictionary and XRECORD retain identities.
            // Rust rewrites provenance flags in untouched embedded fit splines.
            // Compare parameters against the independent in-memory merge; additionally
            // compare the graph after Rust's own save/read without normalizing C++ payloads.
            compare(&actual, &expected, &handles[..3], &handles[4..]);
            for h in &handles[..3] {
                compare_value(
                    &actual.solid_history_graph(*h),
                    &expected_read.solid_history_graph(*h),
                );
            }
        }
    }
    Ok(())
}
