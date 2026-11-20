// SPDX-License-Identifier: MPL-2.0
use acadrust::types::{Matrix4, Transform};
use acadrust::xdata::{ExtendedDataRecord, XDataValue as V};
use acadrust::{DxfReader, DxfWriter, EntityType, Handle, Vector3};
use std::path::Path;
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    crate::geometry::generate(ascii, binary)?;
    let mut doc = DxfReader::from_file(ascii)?.read()?;
    use acadrust::entities::{
        PolyfaceFace, PolyfaceMesh, PolygonMeshEntity, PolygonMeshFlags, PolygonMeshVertex,
        Polyline2D, Polyline3D, Vertex2D,
    };
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        let mut p = Polyline2D::new();
        p.normal = n;
        p.elevation = 2.;
        p.thickness = -1.;
        p.start_width = 0.25;
        p.end_width = 0.5;
        p.close();
        for (i, (x, y)) in [(0., 0.), (8., 0.), (8., 6.), (0., 6.)]
            .into_iter()
            .enumerate()
        {
            let mut v = Vertex2D::new(Vector3::new(x, y, i as f64));
            v.start_width = 0.25 * (i + 1) as f64;
            v.end_width = 0.5 * (i + 1) as f64;
            v.bulge = if i == 3 { -0.5 } else { 0.25 };
            p.add_vertex(v);
        }
        doc.add_entity(EntityType::Polyline2D(p))?;
        let mut p = Polyline3D::new();
        p.close();
        for (x, y, z) in [(0., 0., -2.), (8., 0., 3.), (8., 6., 7.5)] {
            p.add_vertex(Vector3::new(x, y, z));
        }
        doc.add_entity(EntityType::Polyline3D(p))?;
        let mut p = PolygonMeshEntity::new();
        p.normal = n;
        p.m_vertex_count = 2;
        p.n_vertex_count = 3;
        p.flags.insert(PolygonMeshFlags::CLOSED_N);
        for i in 0..2 {
            for j in 0..3 {
                p.vertices.push(PolygonMeshVertex::at(Vector3::new(
                    i as f64,
                    j as f64,
                    (i * 3 - j) as f64,
                )));
            }
        }
        doc.add_entity(EntityType::PolygonMesh(p))?;
        let mut p = PolyfaceMesh::new();
        p.normal = n;
        for (x, y, z) in [
            (0., 0., 0.),
            (4., 0., 1.),
            (4., 4., 2.),
            (0., 4., 3.),
            (2., 2., 5.),
        ] {
            p.add_vertex_xyz(x, y, z);
        }
        p.add_face(PolyfaceFace::triangle(1, -2, 5));
        p.add_face(PolyfaceFace::quad(-1, 2, 3, 4));
        p.add_face(PolyfaceFace::quad(1, 2, -3, 3));
        doc.add_entity(EntityType::PolyfaceMesh(p))?;
    }
    use acadrust::entities::{Face3D, Point, Ray, Solid, Spline, XLine};
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        let a = Vector3::new(1., 2., 3.);
        let b = Vector3::new(4., 2., 5.);
        let c = Vector3::new(4., 6., 7.);
        let d = Vector3::new(0., 6., 9.);
        let mut p = Point::at(a);
        p.normal = n;
        p.thickness = 2.;
        p.x_axis_angle = 0.3;
        doc.add_entity(EntityType::Point(p))?;
        doc.add_entity(EntityType::Ray(Ray::new(a, n)))?;
        doc.add_entity(EntityType::XLine(XLine::new(a, n)))?;
        let mut p = Solid::new(a, b, c, d);
        p.normal = n;
        p.thickness = -2.;
        doc.add_entity(EntityType::Solid(p))?;
        doc.add_entity(EntityType::Face3D(
            Face3D::new(a, b, c, d)
                .with_invisible_edges(acadrust::entities::face3d::InvisibleEdgeFlags::from_bits(9)),
        ))?;
        doc.add_entity(EntityType::Face3D(
            Face3D::triangle(a, b, c)
                .with_invisible_edges(acadrust::entities::face3d::InvisibleEdgeFlags::from_bits(5)),
        ))?;
        let mut p = Spline::new();
        p.degree = 2;
        p.flags.rational = true;
        p.flags.planar = true;
        p.dxf_flags = 0x800;
        p.knots = vec![0., 0., 0., 1., 2., 2., 2.];
        p.control_points = vec![a, b, c, d];
        p.weights = vec![1., 2., 3., 1.];
        p.fit_points = vec![a, d];
        p.normal = n;
        p.control_tolerance = 2e-7;
        p.fit_tolerance = 3e-7;
        p.begin_tangent = Vector3::UNIT_X;
        p.end_tangent = Vector3::UNIT_Y;
        doc.add_entity(EntityType::Spline(p))?;
    }
    use acadrust::entities::{Mesh, MeshFace};
    let mut m = Mesh::create_box(Vector3::new(-1., -2., -3.), Vector3::new(2., 4., 6.));
    m.subdivision_level = 3;
    if let Some(i) = m.find_edge(0, 1) {
        m.set_edge_crease(i, 1.5);
    }
    doc.add_entity(EntityType::Mesh(m))?;
    doc.add_entity(EntityType::Mesh(Mesh::create_plane(
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 1., 2.),
        Vector3::new(0., 3., 1.),
        2,
        3,
    )))?;
    doc.add_entity(EntityType::Mesh(Mesh::create_cylinder(
        Vector3::new(1., -2., 3.),
        2.5,
        -4.,
        7,
        2,
        true,
        true,
    )))?;
    let mut m = Mesh::new();
    m.vertices = vec![
        Vector3::ZERO,
        Vector3::UNIT_X,
        Vector3::UNIT_Y,
        Vector3::UNIT_Z,
        Vector3::new(1., 1., 1.),
    ];
    m.faces = vec![
        MeshFace::new(vec![0, 1, 2, 3, 4]),
        MeshFace::triangle(1, 2, 4),
        MeshFace::quad(0, 1, 4, 3),
    ];
    m.compute_edges();
    doc.add_entity(EntityType::Mesh(m))?;
    doc.add_entity(EntityType::Mesh(Mesh::new()))?;
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    for e in doc.entities_mut() {
        e.common_mut().extended_data.add_record(ExtendedDataRecord {
            application_name: "Geometry".into(),
            values: vec![
                V::String("中文保留".into()),
                V::Point3D(Vector3::new(8., 9., 10.)),
                V::Position3D(Vector3::new(1., 2., 3.)),
                V::Displacement3D(Vector3::new(-2., 1., 4.)),
                V::Direction3D(Vector3::new(0., 1., 0.)),
                V::Direction3D(Vector3::ZERO),
                V::Distance(2.5),
                V::ScaleFactor(0.75),
                V::Real(7.),
            ],
        });
    }
    let mut controls = Vec::new();
    for kind in [
        "LINE",
        "CIRCLE",
        "ARC",
        "ELLIPSE",
        "LWPOLYLINE",
        "POINT",
        "RAY",
        "XLINE",
        "SOLID",
        "3DFACE",
        "SPLINE",
        "MESH",
    ] {
        let mut e = doc
            .entities()
            .find(|e| e.as_entity().entity_type() == kind)
            .unwrap()
            .clone();
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        controls.push(e);
    }
    for kind in 0..4 {
        let mut e = doc
            .entities()
            .find(|e| {
                matches!(
                    (kind, e),
                    (0, EntityType::Polyline2D(_))
                        | (1, EntityType::Polyline3D(_))
                        | (2, EntityType::PolygonMesh(_))
                        | (3, EntityType::PolyfaceMesh(_))
                )
            })
            .unwrap()
            .clone();
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        // Child handles are assigned to the new parent by the DXF writer.
        match &mut e {
            EntityType::PolygonMesh(m) => {
                for v in &mut m.vertices {
                    v.common.handle = Handle::NULL;
                }
            }
            EntityType::PolyfaceMesh(m) => {
                for v in &mut m.vertices {
                    v.common.handle = Handle::NULL;
                }
                for f in &mut m.faces {
                    f.common.handle = Handle::NULL;
                }
            }
            _ => {}
        }
        controls.push(e);
    }
    for e in controls {
        doc.add_entity(e)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
pub(crate) fn operation(step: usize) -> Transform {
    match step {
        1 => Transform::from_translation(Vector3::new(3., -4., 5.)),
        2 => Transform::from_rotation_with_origin(
            Vector3::new(1., 2., 3.),
            0.7,
            Vector3::new(4., -3., 2.),
        ),
        3 => Transform::from_scale(2.),
        4 => Transform::from_scaling(Vector3::new(2., 0.5, 1.5)),
        5 => Transform::from_mirror_x(),
        6 => Transform::from_mirror_plane(Vector3::new(2., -1., 3.), Vector3::new(1., 2., -1.)),
        7 => Transform::from_mirror_line(Vector3::new(1., 1., 5.), Vector3::new(1., 1., -3.)),
        8 => Transform::from_rotation(Vector3::new(0., 1., 0.), 0.7)
            .then(&Transform::from_scaling(Vector3::new(-2., 1., 3.)))
            .then(&Transform::from_translation(Vector3::new(3., -4., 5.))),
        9 => Transform::from_scale(0.),
        11 => {
            let mut t = Transform::identity();
            t.matrix.m[0][0] = f64::INFINITY;
            t
        }
        12 => Transform::from_scaling(Vector3::new(1., 0., 1.)),
        13 => {
            let mut m = Matrix4::identity();
            m.m[0][1] = 0.75;
            m.m[2][0] = -0.2;
            Transform::from_matrix(m)
        }
        _ => Transform::identity(),
    }
}
pub(crate) fn near(a: f64, b: f64) {
    assert!(
        a == b || (a - b).abs() <= 2e-10 * b.abs().max(1.),
        "C++ {a} Rust {b}"
    );
}
pub(crate) fn point(a: Vector3, b: Vector3) {
    near(a.x, b.x);
    near(a.y, b.y);
    near(a.z, b.z);
}
pub(crate) fn compare(a: &EntityType, b: &EntityType) {
    let mut a = a.clone();
    match (&mut a, b) {
        (EntityType::Line(a), EntityType::Line(b)) => {
            point(a.start, b.start);
            point(a.end, b.end);
            point(a.normal, b.normal);
            a.start = b.start;
            a.end = b.end;
            a.normal = b.normal;
        }
        (EntityType::Circle(a), EntityType::Circle(b)) => {
            point(a.center, b.center);
            near(a.radius, b.radius);
            point(a.normal, b.normal);
            a.center = b.center;
            a.radius = b.radius;
            a.normal = b.normal;
        }
        (EntityType::Arc(a), EntityType::Arc(b)) => {
            point(a.center, b.center);
            near(a.radius, b.radius);
            point(a.normal, b.normal);
            near(a.start_angle, b.start_angle);
            near(a.end_angle, b.end_angle);
            a.center = b.center;
            a.radius = b.radius;
            a.normal = b.normal;
            a.start_angle = b.start_angle;
            a.end_angle = b.end_angle;
        }
        (EntityType::Ellipse(a), EntityType::Ellipse(b)) => {
            point(a.center, b.center);
            point(a.major_axis, b.major_axis);
            point(a.normal, b.normal);
            near(a.minor_axis_ratio, b.minor_axis_ratio);
            near(a.start_parameter, b.start_parameter);
            near(a.end_parameter, b.end_parameter);
            a.center = b.center;
            a.major_axis = b.major_axis;
            a.normal = b.normal;
            a.minor_axis_ratio = b.minor_axis_ratio;
            a.start_parameter = b.start_parameter;
            a.end_parameter = b.end_parameter;
        }
        (EntityType::LwPolyline(a), EntityType::LwPolyline(b)) => {
            point(a.normal, b.normal);
            near(a.elevation, b.elevation);
            near(a.constant_width, b.constant_width);
            a.normal = b.normal;
            a.elevation = b.elevation;
            a.constant_width = b.constant_width;
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                near(a.location.x, b.location.x);
                near(a.location.y, b.location.y);
                near(a.bulge, b.bulge);
                near(a.start_width, b.start_width);
                near(a.end_width, b.end_width);
                a.location = b.location;
                a.bulge = b.bulge;
                a.start_width = b.start_width;
                a.end_width = b.end_width;
            }
        }
        (EntityType::Polyline2D(a), EntityType::Polyline2D(b)) => {
            point(a.normal, b.normal);
            near(a.start_width, b.start_width);
            near(a.end_width, b.end_width);
            a.normal = b.normal;
            a.start_width = b.start_width;
            a.end_width = b.end_width;
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                point(a.location, b.location);
                near(a.start_width, b.start_width);
                near(a.end_width, b.end_width);
                near(a.bulge, b.bulge);
                a.location = b.location;
                a.start_width = b.start_width;
                a.end_width = b.end_width;
                a.bulge = b.bulge;
            }
        }
        (EntityType::Polyline3D(a), EntityType::Polyline3D(b)) => {
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                point(a.position, b.position);
                a.position = b.position;
            }
        }
        (EntityType::PolygonMesh(a), EntityType::PolygonMesh(b)) => {
            point(a.normal, b.normal);
            a.normal = b.normal;
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                point(a.location, b.location);
                a.location = b.location;
            }
        }
        (EntityType::PolyfaceMesh(a), EntityType::PolyfaceMesh(b)) => {
            point(a.normal, b.normal);
            a.normal = b.normal;
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                point(a.location, b.location);
                a.location = b.location;
            }
        }
        (EntityType::Point(a), EntityType::Point(b)) => {
            point(a.location, b.location);
            point(a.normal, b.normal);
            near(a.x_axis_angle, b.x_axis_angle);
            a.location = b.location;
            a.normal = b.normal;
            a.x_axis_angle = b.x_axis_angle;
        }
        (EntityType::Ray(a), EntityType::Ray(b)) => {
            point(a.base_point, b.base_point);
            point(a.direction, b.direction);
            a.base_point = b.base_point;
            a.direction = b.direction;
        }
        (EntityType::XLine(a), EntityType::XLine(b)) => {
            point(a.base_point, b.base_point);
            point(a.direction, b.direction);
            a.base_point = b.base_point;
            a.direction = b.direction;
        }
        (EntityType::Solid(a), EntityType::Solid(b)) => {
            point(a.first_corner, b.first_corner);
            point(a.second_corner, b.second_corner);
            point(a.third_corner, b.third_corner);
            point(a.fourth_corner, b.fourth_corner);
            point(a.normal, b.normal);
            a.first_corner = b.first_corner;
            a.second_corner = b.second_corner;
            a.third_corner = b.third_corner;
            a.fourth_corner = b.fourth_corner;
            a.normal = b.normal;
        }
        (EntityType::Face3D(a), EntityType::Face3D(b)) => {
            point(a.first_corner, b.first_corner);
            point(a.second_corner, b.second_corner);
            point(a.third_corner, b.third_corner);
            point(a.fourth_corner, b.fourth_corner);
            a.first_corner = b.first_corner;
            a.second_corner = b.second_corner;
            a.third_corner = b.third_corner;
            a.fourth_corner = b.fourth_corner;
        }
        (EntityType::Spline(a), EntityType::Spline(b)) => {
            assert_eq!(a.control_points.len(), b.control_points.len());
            assert_eq!(a.fit_points.len(), b.fit_points.len());
            for (a, b) in a.control_points.iter().zip(&b.control_points) {
                point(*a, *b);
            }
            for (a, b) in a.fit_points.iter().zip(&b.fit_points) {
                point(*a, *b);
            }
            point(a.normal, b.normal);
            point(a.begin_tangent, b.begin_tangent);
            point(a.end_tangent, b.end_tangent);
            near(a.control_tolerance, b.control_tolerance);
            near(a.fit_tolerance, b.fit_tolerance);
            a.control_points = b.control_points.clone();
            a.fit_points = b.fit_points.clone();
            a.normal = b.normal;
            a.begin_tangent = b.begin_tangent;
            a.end_tangent = b.end_tangent;
            a.control_tolerance = b.control_tolerance;
            a.fit_tolerance = b.fit_tolerance;
        }
        (EntityType::Mesh(a), EntityType::Mesh(b)) => {
            assert_eq!(a.vertices.len(), b.vertices.len());
            for (a, b) in a.vertices.iter().zip(&b.vertices) {
                point(*a, *b);
            }
            a.vertices = b.vertices.clone();
        }
        (EntityType::Light(_),_) | (EntityType::SectionSymbol(_),_) | (EntityType::ViewBorder(_),_) | (EntityType::Extended(_),_) => crate::extended_transform::normalize(&mut a,b),
        (EntityType::Ole2Frame(_),_) => crate::ole_transform::normalize(&mut a,b),
        (EntityType::Table(_),_) => crate::table_transform::normalize(&mut a,b),
        (EntityType::Viewport(_),_) => crate::viewport_transform::normalize(&mut a,b),
        (EntityType::RasterImage(_),_) | (EntityType::Wipeout(_),_) => crate::raster_transform::normalize(&mut a,b),
        (EntityType::Shape(_),_) | (EntityType::Underlay(_),_) => crate::shape_underlay_transform::normalize(&mut a,b),
        (EntityType::Hatch(_),_) => crate::hatch_transform::normalize(&mut a,b),
        (EntityType::Dimension(_),_) => crate::dimension_transform::normalize(&mut a,b),
        (EntityType::Leader(_),_) | (EntityType::Tolerance(_),_) | (EntityType::MLine(_),_) | (EntityType::MultiLeader(_),_) => crate::annotation_transform::normalize(&mut a,b),
        _ => crate::text_transform::normalize(&mut a,b),
    }
    let ax = &a.common().extended_data;
    let bx = &b.common().extended_data;
    assert_eq!(ax.records().len(), bx.records().len());
    for (a, b) in ax.records().iter().zip(bx.records()) {
        assert_eq!(a.application_name, b.application_name);
        assert_eq!(a.values.len(), b.values.len());
        for (a, b) in a.values.iter().zip(&b.values) {
            match (a, b) {
                (V::Position3D(a), V::Position3D(b))
                | (V::Displacement3D(a), V::Displacement3D(b))
                | (V::Direction3D(a), V::Direction3D(b)) => point(*a, *b),
                (V::Distance(a), V::Distance(b)) | (V::ScaleFactor(a), V::ScaleFactor(b)) => {
                    near(*a, *b)
                }
                _ => assert_eq!(a, b),
            }
        }
    }
    a.common_mut().extended_data = bx.clone();
    assert_eq!(&a, b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    for step in 0..14 {
        let path = format!("{prefix}.{step}.dxf");
        assert!(Path::new(&path).exists());
        let result = DxfReader::from_file(&path)?.read()?;
        assert_eq!(source.entities().count(), 265);
        assert_eq!(result.entities().count(), 265);
        let t = operation(step);
        let normal_text = std::fs::read_to_string(format!("{prefix}.{step}.normal"))?;
        let mut normals = std::collections::BTreeMap::new();
        for line in normal_text.lines() {
            let parts: Vec<_> = line.split_whitespace().collect();
            assert_eq!(parts.len(), 4);
            let h = Handle::new(u64::from_str_radix(parts[0], 16).unwrap());
            assert!(normals
                .insert(
                    h,
                    Vector3::new(
                        parts[1].parse().unwrap(),
                        parts[2].parse().unwrap(),
                        parts[3].parse().unwrap()
                    )
                )
                .is_none());
        }

        for e in source.entities() {
            let mut expected = e.clone();
            if e.common().layer != "Control" {
                if step == 0 {
                    expected.translate(Vector3::new(3., -4., 5.));
                } else if [5, 6, 7, 11].contains(&step) {
                    expected.apply_mirror(&t);
                } else {
                    expected.apply_transform(&t);
                }
            }
            if let EntityType::PolyfaceMesh(m) = &mut expected {
                // The pinned DXF writer/reader omit this mesh normal. Verify the native
                // stored header against the in-memory Rust transform before projection.
                point(normals.remove(&e.common().handle).unwrap(), m.normal);
                m.normal = Vector3::UNIT_Z;
            }
            compare(result.get_entity(e.common().handle).unwrap(), &expected);
        }
        assert!(normals.is_empty());
        assert_eq!(source.header, result.header);
        assert_eq!(source.layers, result.layers);
        assert_eq!(source.block_records, result.block_records);
        assert_eq!(source.objects, result.objects);
    }
    Ok(())
}
