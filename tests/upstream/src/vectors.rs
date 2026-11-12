// SPDX-License-Identifier: MPL-2.0
use acadrust::{Vector2, Vector3};

pub fn verify(input: &str, cpp: &str) -> acadrust::Result<()> {
    let inputs = std::fs::read_to_string(input)?;
    let results = std::fs::read_to_string(cpp)?;
    let mut rows = results.lines();
    let mut count = 0;
    for line in inputs
        .lines()
        .filter(|s| !s.is_empty() && !s.starts_with('#'))
    {
        let mut fields = line.split_whitespace();
        let case = fields.next().unwrap();
        let coordinates: Vec<f64> = fields.map(|s| s.parse().unwrap()).collect();
        let &[ax, ay, az, bx, by, bz] = coordinates.as_slice() else {
            panic!("invalid vector input: {case}")
        };
        let (a2, b2) = (Vector2::new(ax, ay), Vector2::new(bx, by));
        let (a, b) = (Vector3::new(ax, ay, az), Vector3::new(bx, by, bz));
        let (n2, cross, n3) = (a2.normalize(), a.cross(&b), a.normalize());
        let mut row = rows.next().expect("missing C++ result").split_whitespace();
        assert_eq!(row.next(), Some(case));
        for (field, rust) in [
            ("2D.length", a2.length()),
            ("2D.dot", a2.dot(&b2)),
            ("2D.cross", a2.cross(&b2)),
            ("2D.normalize.x", n2.x),
            ("2D.normalize.y", n2.y),
            ("3D.length", a.length()),
            ("3D.dot", a.dot(&b)),
            ("3D.cross.x", cross.x),
            ("3D.cross.y", cross.y),
            ("3D.cross.z", cross.z),
            ("3D.normalize.x", n3.x),
            ("3D.normalize.y", n3.y),
            ("3D.normalize.z", n3.z),
        ] {
            let actual: f64 = row.next().expect("missing C++ component").parse().unwrap();
            assert_eq!(
                actual.to_bits(),
                rust.to_bits(),
                "{case} {field}: C++ {actual}, Rust {rust}"
            );
            count += 1;
        }
        assert!(row.next().is_none(), "extra C++ component for {case}");
    }
    assert!(
        count > 0 && rows.next().is_none(),
        "unexpected result row count"
    );
    println!("{count} vector components matched bit for bit");
    Ok(())
}
