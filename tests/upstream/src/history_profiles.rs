// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::solid3d::AcisRevision;
use acadrust::entities::*;
use acadrust::objects::*;
use acadrust::{CadDocument, DxfReader, DxfWriter, Handle, Vector2, Vector3};
use std::{fs, path::Path};
pub(crate) fn profiles() -> Vec<EmbeddedEntity> {
    let mut point = Point::new();
    point.location = Vector3::new(2., 3., 4.);
    point.thickness = 0.5;
    point.x_axis_angle = 0.25;
    let mut arc = Arc::new();
    arc.center = Vector3::new(3., 4., 5.);
    arc.radius = 2.5;
    arc.normal = Vector3::new(0., 1., 0.);
    arc.start_angle = 0.2;
    arc.end_angle = 2.3;
    let mut circle = Circle::from_center_radius(Vector3::new(4., 5., 6.), 3.5);
    circle.normal = Vector3::new(1., 0., 0.);
    let mut ellipse = Ellipse::new();
    ellipse.center = Vector3::new(2., 3., 4.);
    ellipse.major_axis = Vector3::new(3., 1., 0.);
    ellipse.minor_axis_ratio = 0.6;
    ellipse.start_parameter = 0.2;
    ellipse.end_parameter = 5.1;
    let mut poly = LwPolyline::new();
    poly.is_closed = true;
    poly.plinegen = true;
    poly.elevation = 2.;
    poly.thickness = 0.5;
    poly.constant_width = 0.25;
    poly.vertices = (0..4)
        .map(|i| LwVertex {
            location: Vector2::new(i as f64, (i * i) as f64),
            bulge: if i == 3 { -0.5 } else { 0.25 },
            start_width: 0.1 + i as f64,
            end_width: 0.2,
            vertex_id: i + 10,
        })
        .collect();
    let mut spline = Spline::new();
    spline.degree = 2;
    spline.flags.rational = true;
    spline.knots = vec![0., 0., 0., 1., 1., 1.];
    spline.control_points = vec![
        Vector3::new(0., 0., 0.),
        Vector3::new(2., 3., 4.),
        Vector3::new(4., 2., 1.),
    ];
    spline.weights = vec![1., 0.5, 1.];
    spline.knot_parameterization = 15;
    spline.cv_frame_visible = true;
    let mut fit = Spline::new();
    fit.degree = 3;
    fit.fit_points = vec![
        Vector3::new(1., 2., 3.),
        Vector3::new(3., 4., 5.),
        Vector3::new(1., 2., 3.),
    ];
    fit.begin_tangent = Vector3::new(1., 0., 0.);
    fit.end_tangent = Vector3::new(0., 1., 0.);
    fit.knot_parameterization = 1;
    let mut region = Region::new();
    region.acis_data = AcisData::from_sab(acis::SabWriter::write(&acis::primitives::build_box(
        [1., 2., 3.],
        4.,
        5.,
        6.,
    )));
    region.point_of_reference = Vector3::new(2., 3., 4.);
    region.wires = vec![Wire {
        wire_type: WireType::VisibleEdge,
        selection_marker: -7,
        acis_index: 42,
        color: acadrust::Color::Index(5),
        points: vec![Vector3::new(1., 2., 3.), Vector3::new(4., 5., 6.)],
        has_transform: true,
        scale: Vector3::new(2., 2., 2.),
        translation: Vector3::new(3., 4., 5.),
        has_rotation: true,
        ..Default::default()
    }];
    let mut sil = Silhouette::new(12345);
    sil.add_wire(region.wires[0].clone());
    region.silhouettes.push(sil);
    region.acis_data.wireframe_isolines = 4;
    region.acis_data.materials = vec![AcisMaterial {
        array_index: 2,
        absolute_reference: 3,
        material_handle: Some(Handle::new(0xAABB)),
    }];
    region.acis_data.revision = AcisRevision {
        has_guid: true,
        major: 123,
        minor1: 4,
        minor2: 5,
        bytes: [1, 2, 3, 4, 5, 6, 7, 8],
        end_marker: 789,
    };
    region.acis_data.extra_acis_data = Some(Box::new(AcisData::from_sat(
        "700 0 0 0\nwire $-1 -1 $-1 $-1 #\n",
    )));
    vec![
        EmbeddedEntity::Point(point),
        EmbeddedEntity::Line(Line::from_points(
            Vector3::new(1., 2., 3.),
            Vector3::new(4., 5., 6.),
        )),
        EmbeddedEntity::Arc(arc),
        EmbeddedEntity::Circle(circle),
        EmbeddedEntity::Ellipse(ellipse),
        EmbeddedEntity::LwPolyline(poly),
        EmbeddedEntity::Spline(spline),
        EmbeddedEntity::Spline(fit),
        EmbeddedEntity::Ray(Ray::new(Vector3::new(1., 2., 3.), Vector3::new(2., 3., 4.))),
        EmbeddedEntity::XLine(XLine::new(
            Vector3::new(4., 5., 6.),
            Vector3::new(4., 3., 2.),
        )),
        EmbeddedEntity::Region(region),
        EmbeddedEntity::Unknown {
            type_code: 777,
            bit_count: 16,
            bytes: vec![0x25, 0xa0],
        },
        EmbeddedEntity::Unknown {
            type_code: 19,
            bit_count: 8,
            bytes: vec![0],
        },
    ]
}
fn operations(d: &mut CadDocument, mut f: impl FnMut(&mut SolidHistoryOperation)) {
    for o in d.objects.values_mut() {
        if let ObjectType::DynamicBlock(o) = o {
            if let DynamicBlockData::SolidHistoryNode(op) = &mut o.data {
                f(op);
            }
        }
    }
}
pub fn generate(source: &str, out: &str) -> acadrust::Result<()> {
    fs::create_dir_all(out)?;
    for version in ["AC1021", "AC1024", "AC1027", "AC1032"] {
        let mut d =
            DxfReader::from_file(Path::new(source).join(format!("{version}-ascii.dxf")))?.read()?;
        let p = profiles();
        operations(&mut d, |op| match op {
            SolidHistoryOperation::Sweep(v) => {
                v.sweep_entity = Some(p[5].clone());
                v.path_entity = Some(p[6].clone());
            }
            SolidHistoryOperation::Extrusion(v) => {
                v.sweep_entity = Some(p[2].clone());
                v.path_entity = Some(p[9].clone());
            }
            SolidHistoryOperation::Revolve(v) => v.sweep_entity = Some(p[3].clone()),
            SolidHistoryOperation::Loft(v) => {
                v.cross_sections = p.clone();
                v.guides = vec![p[7].clone(), p[8].clone()];
                v.path_entity = Some(p[1].clone());
                v.parameters = Some(SolidHistoryLoftParameters {
                    path_entity: Some(p[5].clone()),
                    section_counts: vec![1; p.len()],
                    ..Default::default()
                });
            }
            _ => {}
        });
        for binary in [false, true] {
            let w = if binary {
                DxfWriter::new_binary(&d)
            } else {
                DxfWriter::new(&d)
            };
            w.write_to_file(Path::new(out).join(format!(
                "{version}-{}.dxf",
                if binary { "binary" } else { "ascii" }
            )))?;
        }
    }
    Ok(())
}
pub(crate) fn change(v: &mut EmbeddedEntity) {
    match v {
        EmbeddedEntity::Region(v) => {
            v.acis_data.sab_data =
                acis::SabWriter::write(&acis::primitives::build_box([2., 3., 4.], 5., 6., 7.));
            v.point_of_reference.x += 10.;
            v.wires[0].points[0].z += 10.;
            v.acis_data.revision.major += 1;
        }
        EmbeddedEntity::Point(v) => {
            v.location.x += 10.;
            v.thickness += 0.25;
        }
        EmbeddedEntity::Line(v) => v.end.z += 10.,
        EmbeddedEntity::Arc(v) => {
            v.center.x += 10.;
            v.end_angle += 0.2;
        }
        EmbeddedEntity::Circle(v) => {
            v.center.x += 10.;
            v.radius += 0.5;
        }
        EmbeddedEntity::Ellipse(v) => {
            v.center.x += 10.;
            v.major_axis.y += 0.5;
        }
        EmbeddedEntity::LwPolyline(v) => {
            v.vertices[0].location.x += 10.;
            v.vertices[3].bulge = -0.75;
            v.vertices[0].start_width = 2.25;
        }
        EmbeddedEntity::Spline(v) => {
            if !v.control_points.is_empty() {
                v.control_points[0].x += 10.;
                v.weights[1] = 0.75;
            }
            if !v.fit_points.is_empty() {
                v.fit_points[1].z += 10.;
            }
        }
        EmbeddedEntity::Ray(v) => v.base_point.x += 10.,
        EmbeddedEntity::XLine(v) => v.base_point.x += 10.,
        _ => {}
    }
}
fn changed(d: &mut CadDocument) {
    operations(d, |op| match op {
        SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => {
            v.sweep_entity.as_mut().map(change);
            v.path_entity.as_mut().map(change);
        }
        SolidHistoryOperation::Revolve(v) => {
            v.sweep_entity.as_mut().map(change);
        }
        SolidHistoryOperation::Loft(v) => {
            v.cross_sections.iter_mut().for_each(change);
            let mut fresh = Spline::new();
            fresh.degree = 20;
            fresh.control_points = vec![
                Vector3::new(0., 0., 0.),
                Vector3::new(1., 2., 3.),
                Vector3::new(4., 5., 6.),
            ];
            v.cross_sections.push(EmbeddedEntity::Spline(fresh));
            let mut fresh_region = Region::new();
            fresh_region.acis_data = AcisData::from_sat(
                &acis::primitives::build_box([5., 6., 7.], 2., 3., 4.).to_sat_string(),
            );
            v.cross_sections.push(EmbeddedEntity::Region(fresh_region));
            v.cross_sections.push(EmbeddedEntity::Region(Region::new()));
            v.parameters
                .as_mut()
                .unwrap()
                .section_counts
                .extend([1, 1, 1]);
            v.guides.iter_mut().for_each(change);
            v.path_entity.as_mut().map(change);
            if let Some(p) = &mut v.parameters {
                p.path_entity.as_mut().map(change);
            }
        }
        _ => {}
    });
}
fn nodes(d: &CadDocument) -> Vec<(Handle, SolidHistoryOperation)> {
    let mut v = vec![];
    for (h, o) in &d.objects {
        if let ObjectType::DynamicBlock(o) = o {
            if let DynamicBlockData::SolidHistoryNode(op) = &o.data {
                v.push((*h, op.clone()));
            }
        }
    }
    v.sort_by_key(|x| x.0.value());
    v
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let mut expected = DxfReader::from_file(input)?.read()?;
    changed(&mut expected);
    for binary in [false, true] {
        let ext = if binary { "binary" } else { "ascii" };
        let oracle = format!("{prefix}.oracle.{ext}.dxf");
        let w = if binary {
            DxfWriter::new_binary(&expected)
        } else {
            DxfWriter::new(&expected)
        };
        w.write_to_file(&oracle)?;
        let want = DxfReader::from_file(&oracle)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{ext}.dxf"))?.read()?;
        super::history_author::compare(&nodes(&want), &nodes(&actual));
        let mut wire = want.clone();
        operations(&mut wire, |op| {
            if let SolidHistoryOperation::Sweep(v) = op {
                let data = AcisData::from_sat("700 0 0 0\nwire $-1 -1 $-1 $-1 #\n");
                v.sweep_entity = Some(EmbeddedEntity::Body {
                    type_code: 15,
                    acis_data: data.clone(),
                });
                v.path_entity = Some(EmbeddedEntity::Body {
                    type_code: 16,
                    acis_data: data,
                });
            }
        });
        let wire_oracle = format!("{prefix}.wire.oracle.{ext}.dxf");
        let w = if binary {
            DxfWriter::new_binary(&wire)
        } else {
            DxfWriter::new(&wire)
        };
        w.write_to_file(&wire_oracle)?;
        let wire_expected = DxfReader::from_file(&wire_oracle)?.read()?;
        let wire_actual = DxfReader::from_file(format!("{prefix}.wire.{ext}.dxf"))?.read()?;
        super::history_author::compare(&nodes(&wire_expected), &nodes(&wire_actual));
        // Compare modeler history graph ownership as well as all operation payloads.
        for e in want.entities() {
            let h = e.common().handle;
            super::history_author::compare(
                &want.solid_history_graph(h),
                &actual.solid_history_graph(h),
            );
        }
    }
    Ok(())
}
