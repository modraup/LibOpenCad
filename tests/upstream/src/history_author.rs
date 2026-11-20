// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Circle, EmbeddedEntity, Line};
use acadrust::objects::*;
use acadrust::{CadDocument, Color, DxfReader, DxfWriter, EntityType, Handle, Vector3};
use std::{fs, path::Path};
fn object(d: &CadDocument, h: Handle) -> &DynamicBlockObject {
    let ObjectType::DynamicBlock(v) = &d.objects[&h] else {
        panic!()
    };
    v
}
fn op(d: &CadDocument, h: Handle) -> SolidHistoryOperation {
    let DynamicBlockData::SolidHistoryNode(v) = &object(d, h).data else {
        panic!()
    };
    v.clone()
}
fn line() -> EmbeddedEntity {
    EmbeddedEntity::Line(Line::from_points(
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 5., 6.),
    ))
}
fn circle() -> EmbeddedEntity {
    EmbeddedEntity::Circle(Circle::from_center_radius(Vector3::new(2., 3., 4.), 2.5))
}
pub fn generate(source: &str, out: &str) -> acadrust::Result<()> {
    fs::create_dir_all(out)?;
    for version in ["AC1021", "AC1024", "AC1027", "AC1032"] {
        let mut d =
            DxfReader::from_file(Path::new(source).join(format!("{version}-ascii.dxf")))?.read()?;
        for o in d.objects.values_mut() {
            if let ObjectType::DynamicBlock(o) = o {
                if let DynamicBlockData::SolidHistoryNode(v) = &mut o.data {
                    match v {
                        SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => {
                            v.sweep_entity = Some(circle());
                            v.path_entity = Some(line());
                        }
                        SolidHistoryOperation::Revolve(v) => v.sweep_entity = Some(line()),
                        SolidHistoryOperation::Loft(v) => {
                            v.cross_sections = vec![circle(), line()];
                            v.guides = vec![line()];
                            v.path_entity = Some(circle());
                            v.parameters = Some(SolidHistoryLoftParameters {
                                section_counts: vec![1, 1],
                                path_entity: if version == "AC1032" {
                                    Some(line())
                                } else {
                                    None
                                },
                                ..Default::default()
                            });
                        }
                        _ => {}
                    }
                }
            }
        }
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
pub(crate) fn changed(mut op: SolidHistoryOperation) -> SolidHistoryOperation {
    let b = op.base_mut().unwrap();
    b.eval.major += 2;
    b.transform[12] += 0.5;
    b.color = Color::from_rgb(12, 34, 56);
    macro_rules! version {
        ($v:expr) => {{
            $v.operation_major += 3;
            $v.operation_minor += 4;
        }};
    }
    match &mut op {
        SolidHistoryOperation::Box(v) | SolidHistoryOperation::Wedge(v) => {
            version!(v);
            v.length += 1.;
            v.width += 2.;
            v.height += 3.;
        }
        SolidHistoryOperation::Sphere(v) => {
            version!(v);
            v.radius += 2.;
        }
        SolidHistoryOperation::Cylinder(v) => {
            version!(v);
            v.height += 1.;
            v.major_radius += 2.;
            v.minor_radius += 3.;
            v.x_radius += 4.;
        }
        SolidHistoryOperation::Cone(v) => {
            version!(v);
            v.height += 1.;
            v.base_x_radius += 2.;
            v.base_y_radius += 3.;
            v.top_radius += 4.;
        }
        SolidHistoryOperation::Pyramid(v) => {
            version!(v);
            v.height += 1.;
            v.sides = 7;
            v.radius += 2.;
            v.top_radius += 3.;
        }
        SolidHistoryOperation::Torus(v) => {
            version!(v);
            v.major_radius += 2.;
            v.minor_radius += 1.;
        }
        SolidHistoryOperation::Boolean(v) => {
            version!(v);
            v.operation = 1;
        }
        SolidHistoryOperation::Brep(v) => {
            version!(v);
            v.acis_data = acadrust::entities::AcisData::from_sat(
                &acadrust::entities::acis::primitives::build_box([2., 3., 4.], 5., 6., 7.)
                    .to_sat_string(),
            );
        }
        SolidHistoryOperation::Fillet(v) => {
            version!(v);
            v.method = 2;
            v.edges = vec![1, 3, 5];
            v.radii = vec![0.25, 0.5, 1.];
            v.start_setbacks = vec![0.1, 0.2];
            v.end_setbacks = vec![0.3];
        }
        SolidHistoryOperation::Chamfer(v) => {
            version!(v);
            v.method = 1;
            v.base_distance = 3.;
            v.other_distance = 4.;
            v.edges = vec![2, 4, 6];
            v.base_face = 7;
        }
        SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => {
            version!(v);
            v.direction = Vector3::new(1., 2., 3.);
            v.draft_angle = 0.2;
            v.start_draft_distance = 2.;
            v.end_draft_distance = 3.;
            v.twist_angle = 0.3;
            v.scale_factor = 2.;
            v.align_angle = 0.4;
            v.sweep_entity_transform[12] = 4.;
            v.path_entity_transform[13] = 5.;
            v.has_align_start = true;
            v.align_option = 2;
            v.miter_option = 1;
            v.align_start = false;
            v.bank = true;
            v.flags_294_296 = [true, false, true];
            v.reference_point = Vector3::new(4., 5., 6.);
        }
        SolidHistoryOperation::Loft(v) => {
            version!(v);
            v.options = SolidHistoryLoftOptions {
                surface_option: 3,
                start_draft_angle: 0.4,
                end_draft_angle: 0.5,
                start_magnitude: 2.,
                end_magnitude: 3.,
                flags: [true, false, true, false, true, false, true, false],
            };
            let s = v.parameters.get_or_insert_with(Default::default);
            s.normals = 6;
            s.start_draft_angle = 0.6;
            s.end_draft_angle = 0.7;
            s.start_magnitude = 4.;
            s.end_magnitude = 5.;
            s.start_bulge = 0.8;
            s.end_bulge = 0.9;
            s.section_counts = vec![1, 1];
        }
        SolidHistoryOperation::Revolve(v) => {
            version!(v);
            v.axis_point = Vector3::new(1., 2., 3.);
            v.direction = Vector3::new(0., 1., 0.);
            v.revolve_angle = 1.2;
            v.start_angle = 0.3;
            v.draft_angle = 0.4;
            v.field_44 = 2.;
            v.field_45 = 3.;
            v.twist_angle = 0.5;
            v.flag_290 = true;
            v.close_to_axis = true;
        }
        _ => panic!(),
    }
    op
}
fn replacement() -> SolidHistoryOperation {
    SolidHistoryOperation::Cone(SolidHistoryCone {
        base: SolidHistoryNodeBase {
            eval: BlockEvalExpression {
                major: 1,
                value_code: -9999,
                parent_id: -1,
                ..Default::default()
            },
            major: 1,
            transform: [
                1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.,
            ],
            ..Default::default()
        },
        height: 8.,
        base_x_radius: 4.,
        base_y_radius: 3.,
        top_radius: 1.,
        ..Default::default()
    })
}
pub(crate) fn compare(a: &impl std::fmt::Debug, b: &impl std::fmt::Debug) {
    let a = format!("{a:?}");
    let b = format!("{b:?}");
    let parts = |s: &str| {
        s.split(|c: char| c.is_whitespace() || "[]{}(),:".contains(c))
            .filter(|s| !s.is_empty())
            .map(str::to_owned)
            .collect::<Vec<_>>()
    };
    let aa = parts(&a);
    let bb = parts(&b);
    assert_eq!(aa.len(), bb.len(), "{a}\n!=\n{b}");
    for (x, y) in aa.iter().zip(&bb) {
        match (x.parse::<f64>(), y.parse::<f64>()) {
            (Ok(x), Ok(y)) => assert!(
                (x - y).abs() <= 1e-10 * x.abs().max(y.abs()).max(1.),
                "{x} != {y}"
            ),
            _ => assert_eq!(x, y, "{a}\n!=\n{b}"),
        }
    }
}
fn roundtrip(d: &CadDocument, binary: bool, p: &str) -> acadrust::Result<CadDocument> {
    let w = if binary {
        DxfWriter::new_binary(d)
    } else {
        DxfWriter::new(d)
    };
    w.write_to_file(p)?;
    DxfReader::from_file(p)?.read()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let manifest = fs::read_to_string(format!("{prefix}.manifest"))?;
    let h = manifest
        .split_whitespace()
        .map(|v| Handle::new(u64::from_str_radix(v, 16).unwrap()))
        .collect::<Vec<_>>();
    let (solid, surface, fresh) = (h[0], h[1], h[2]);
    let mut edited = source.clone();
    for id in source.solid_history_graph(surface).unwrap().nodes {
        edited
            .update_solid_history_step(surface, changed(op(&source, id)))
            .unwrap();
    }
    let mut current = source.clone();
    current.update_solid_history(solid, replacement()).unwrap();
    let mut identity_current = source.clone();
    identity_current
        .update_solid_history(
            solid,
            source.solid_history_operation(solid).unwrap().clone(),
        )
        .unwrap();
    let mut operations = source
        .solid_history_graph(surface)
        .unwrap()
        .nodes
        .iter()
        .map(|h| op(&source, *h))
        .collect::<Vec<_>>();
    operations.sort_by_key(|o| o.base().unwrap().eval.node_id);
    let mut initial = operations[0].clone();
    initial.base_mut().unwrap().step_id = 5;
    let mut authored = source.clone();
    authored
        .create_solid_history(solid, initial.clone())
        .unwrap();
    for o in operations.iter().skip(1) {
        authored.append_solid_history(solid, o.clone()).unwrap();
    }
    let mut surface_new = source.clone();
    surface_new.create_solid_history(surface, initial).unwrap();
    surface_new
        .append_solid_history(surface, replacement())
        .unwrap();
    for binary in [false, true] {
        let enc = if binary { "binary" } else { "ascii" };
        for (label, expected, target) in [
            ("edited", &edited, surface),
            ("current", &current, solid),
            ("identity-current", &identity_current, solid),
            ("authored", &authored, solid),
            ("surface", &surface_new, surface),
        ] {
            let expected = roundtrip(
                expected,
                binary,
                &format!("{prefix}.{label}.{enc}.expected.dxf"),
            )?;
            let actual = DxfReader::from_file(format!("{prefix}.{label}.{enc}.dxf"))?.read()?;
            compare(
                &actual.solid_history_tree(target),
                &expected.solid_history_tree(target),
            );
            let ag = actual.solid_history_graph(target).unwrap();
            let eg = expected.solid_history_graph(target).unwrap();
            assert_eq!(ag.nodes.len(), eg.nodes.len());
            for (a, b) in ag.nodes.iter().zip(&eg.nodes) {
                compare(&op(&actual, *a), &op(&expected, *b));
                assert!(actual.owner_chain_reaches(object(&actual, *a).owner, ag.root));
            }
            assert_eq!(object(&actual, ag.root).owner, target);
            for class in expected.classes.iter().filter(|c| {
                c.dxf_name.starts_with("ACSH_") || c.dxf_name == "ACAD_EVALUATION_GRAPH"
            }) {
                assert_eq!(
                    actual
                        .classes
                        .get_by_name(&class.dxf_name)
                        .unwrap()
                        .instance_count,
                    class.instance_count,
                    "history class count {}",
                    class.dxf_name
                );
            }
            for e in source.entities() {
                let id = e.common().handle;
                if id != target {
                    assert_eq!(actual.get_entity(id).unwrap(), e);
                    compare(
                        &actual.solid_history_tree(id),
                        &source.solid_history_tree(id),
                    );
                }
            }
            if label == "edited" || label == "current" {
                assert_eq!(ag.nodes, eg.nodes);
                assert_eq!(ag.root, eg.root);
            }
            if label == "authored" || label == "surface" {
                let old = source.solid_history_graph(target).unwrap();
                assert!(!actual.objects.contains_key(&old.root));
                for id in old.nodes {
                    assert!(!actual.objects.contains_key(&id));
                }
            }
            let original = source.get_entity(target).unwrap();
            let updated = actual.get_entity(target).unwrap();
            match (original, updated) {
                (EntityType::Solid3D(a), EntityType::Solid3D(b)) => {
                    assert_eq!(a.acis_data, b.acis_data)
                }
                (EntityType::Surface(a), EntityType::Surface(b)) => {
                    assert_eq!(a.acis_data, b.acis_data)
                }
                _ => panic!(),
            }
        }
        let d = DxfReader::from_file(format!("{prefix}.fresh.{enc}.dxf"))?.read()?;
        let mut legacy =
            DxfReader::from_file(format!("{prefix}.legacy-before.{enc}.dxf"))?.read()?;
        assert!(legacy
            .solid_history_graph(fresh)
            .unwrap()
            .evaluation_graph
            .is_none());
        legacy.append_solid_history(fresh, replacement()).unwrap();
        let expected = roundtrip(
            &legacy,
            binary,
            &format!("{prefix}.legacy.{enc}.expected.dxf"),
        )?;
        let actual = DxfReader::from_file(format!("{prefix}.legacy.{enc}.dxf"))?.read()?;
        compare(
            &actual.solid_history_tree(fresh),
            &expected.solid_history_tree(fresh),
        );
        let chain = d.solid_history_operations(fresh).unwrap();
        assert_eq!(chain.len(), 2);
        assert!(matches!(&chain[0], SolidHistoryOperation::Box(_)));
        let SolidHistoryOperation::Cone(v) = &chain[1] else {
            panic!()
        };
        assert_eq!(
            (v.height, v.base_x_radius, v.base_y_radius, v.top_radius),
            (8., 4., 3., 1.)
        );
    }
    println!("All 15 history parameter types, creation, append, replacement, step editing and profiles match Rust");
    Ok(())
}
