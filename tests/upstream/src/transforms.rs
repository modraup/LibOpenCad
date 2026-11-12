// SPDX-License-Identifier: MPL-2.0
use acadrust::types::{Matrix3, Matrix4, Transform, Vector3};

pub fn verify(input: &str, cpp: &str) -> acadrust::Result<()> {
    let inputs = std::fs::read_to_string(input)?;
    let results = std::fs::read_to_string(cpp)?;
    let mut rows = results.lines();
    let mut count = 0;
    let mut max_error = 0.0_f64;
    for line in inputs
        .lines()
        .filter(|s| !s.is_empty() && !s.starts_with('#'))
    {
        let mut fields = line.split_whitespace();
        let case = fields.next().unwrap();
        let values: Vec<f64> = fields.map(|s| s.parse().unwrap()).collect();
        let &[px, py, pz, dx, dy, dz] = values.as_slice() else {
            panic!("invalid input {case}")
        };
        let (p, d) = (Vector3::new(px, py, pz), Vector3::new(dx, dy, dz));
        let origin = Vector3::new(2., -1., 3.);
        let axis = Vector3::new(1., -2., 3.);
        let scale = Vector3::new(2., 3., -1.);
        let angle = 0.73;
        let t = Transform::from_translation(Vector3::new(5., -2., 1.));
        let r = Transform::from_rotation(axis, angle);
        let s = Transform::from_scaling(scale);
        let linear = Matrix3::rotation_z(angle) * Matrix3::scaling(scale.x, scale.y, scale.z);
        let mut near_zero_w = Matrix4::identity();
        near_zero_w.m[3][3] = 1e-11;

        let mut compare = |op: &str, point: Vector3, dir: Vector3| {
            let mut row = rows.next().expect("missing C++ result").split_whitespace();
            assert_eq!(row.next(), Some(case));
            assert_eq!(row.next(), Some(op));
            for (field, expected) in [
                ("point.x", point.x),
                ("point.y", point.y),
                ("point.z", point.z),
                ("direction.x", dir.x),
                ("direction.y", dir.y),
                ("direction.z", dir.z),
            ] {
                let actual: f64 = row.next().unwrap().parse().unwrap();
                let error = (actual - expected).abs();
                assert!(
                    error <= 1e-12 * expected.abs().max(1.),
                    "{case} {op} {field}: C++ {actual}, Rust {expected}"
                );
                max_error = max_error.max(error);
                count += 1;
            }
            assert!(row.next().is_none(), "extra C++ component");
        };
        for (op, mat) in [
            ("matrix3_product", linear),
            ("matrix3_transpose", linear.transpose()),
            ("matrix3_inverse", linear.inverse().unwrap()),
            ("arbitrary_axis", Matrix3::arbitrary_axis(axis)),
            ("arbitrary_axis_z", Matrix3::arbitrary_axis(Vector3::UNIT_Z)),
        ] {
            compare(op, mat * p, mat * d);
        }
        let expanded = Matrix4::from_matrix3(linear);
        compare(
            "matrix3_to_4",
            expanded.transform_point(p),
            expanded.transform_direction(d),
        );
        let reduced = (t.matrix * r.matrix * s.matrix).to_matrix3();
        compare("matrix4_to_3", reduced * p, reduced * d);
        for (op, mat) in [
            ("rotation_x", Matrix4::rotation_x(angle)),
            ("rotation_y", Matrix4::rotation_y(angle)),
            ("rotation_z", Matrix4::rotation_z(angle)),
            ("projective", t.matrix.transpose()),
            ("near_zero_w", near_zero_w),
        ] {
            compare(op, mat.transform_point(p), mat.transform_direction(d));
        }
        for (op, transform) in [
            ("translation", t),
            ("rotation", r),
            ("uniform", Transform::from_scale(0.5)),
            ("scaling", s),
            (
                "scale_about",
                Transform::from_scaling_with_origin(scale, origin),
            ),
            (
                "rotate_about",
                Transform::from_rotation_with_origin(axis, angle, origin),
            ),
            ("translate_then_scale", t.then(&s)),
            ("translate_compose_scale", t.compose(&s)),
            ("then", t.then(&r).then(&s)),
            ("compose", t.compose(&r).compose(&s)),
            ("product", t * r * s),
            ("mirror_plane", Transform::from_mirror_plane(origin, axis)),
            ("mirror_line", Transform::from_mirror_line(origin, axis)),
        ] {
            compare(op, transform.apply(p), transform.apply_rotation(d));
        }
    }
    assert!(
        count > 0 && rows.next().is_none(),
        "unexpected result count"
    );
    println!("{count} transform components matched; maximum absolute error = {max_error:e}");
    Ok(())
}
