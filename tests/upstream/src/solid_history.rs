// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::primitives::build_box;
use acadrust::entities::{AcisData, Solid3D, Surface, SurfaceKind};
use acadrust::objects::*;
use acadrust::{CadDocument, Color, DxfReader, DxfVersion, DxfWriter, EntityType, Handle};
use std::{fs, path::Path};
fn operation(k: usize) -> SolidHistoryOperation {
    let base = SolidHistoryNodeBase::new(1);
    match k {
        0 => SolidHistoryOperation::Box(SolidHistoryBox {
            base,
            length: 2.,
            width: 3.,
            height: 4.,
            ..Default::default()
        }),
        1 => SolidHistoryOperation::Wedge(SolidHistoryBox {
            base,
            length: 3.,
            width: 4.,
            height: 5.,
            ..Default::default()
        }),
        2 => SolidHistoryOperation::Sphere(SolidHistorySphere {
            base,
            radius: 3.,
            ..Default::default()
        }),
        3 => SolidHistoryOperation::Cylinder(SolidHistoryCylinder {
            base,
            height: 4.,
            major_radius: 3.,
            minor_radius: 2.,
            x_radius: 1.,
            ..Default::default()
        }),
        4 => SolidHistoryOperation::Cone(SolidHistoryCone {
            base,
            height: 4.,
            base_x_radius: 3.,
            base_y_radius: 2.,
            top_radius: 1.,
            ..Default::default()
        }),
        5 => SolidHistoryOperation::Pyramid(SolidHistoryPyramid {
            base,
            height: 5.,
            sides: 6,
            radius: 4.,
            top_radius: 1.,
            ..Default::default()
        }),
        6 => SolidHistoryOperation::Torus(SolidHistoryTorus {
            base,
            major_radius: 4.,
            minor_radius: 1.,
            ..Default::default()
        }),
        7 => SolidHistoryOperation::Boolean(SolidHistoryBoolean {
            base,
            operation: 2,
            first_operand: 1,
            second_operand: 2,
            ..Default::default()
        }),
        8 => SolidHistoryOperation::Brep(SolidHistoryBrep {
            base,
            acis_data: AcisData::from_sat(&build_box([0., 0., 0.], 1., 2., 3.).to_sat_string()),
            ..Default::default()
        }),
        9 => SolidHistoryOperation::Fillet(SolidHistoryFillet {
            base,
            edges: vec![1, 3],
            radii: vec![0.2, 0.4],
            ..Default::default()
        }),
        10 => SolidHistoryOperation::Chamfer(SolidHistoryChamfer {
            base,
            base_distance: 2.,
            other_distance: 3.,
            edges: vec![2, 4],
            ..Default::default()
        }),
        11 => SolidHistoryOperation::Sweep(SolidHistorySweep {
            base,
            ..Default::default()
        }),
        12 => SolidHistoryOperation::Extrusion(SolidHistorySweep {
            base,
            ..Default::default()
        }),
        13 => SolidHistoryOperation::Loft(SolidHistoryLoft {
            base,
            ..Default::default()
        }),
        14 => SolidHistoryOperation::Revolve(SolidHistoryRevolve {
            base,
            ..Default::default()
        }),
        _ => panic!(),
    }
}
pub(crate) fn kind(o: &SolidHistoryOperation) -> usize {
    match o {
        SolidHistoryOperation::Box(_) => 0,
        SolidHistoryOperation::Wedge(_) => 1,
        SolidHistoryOperation::Sphere(_) => 2,
        SolidHistoryOperation::Cylinder(_) => 3,
        SolidHistoryOperation::Cone(_) => 4,
        SolidHistoryOperation::Pyramid(_) => 5,
        SolidHistoryOperation::Torus(_) => 6,
        SolidHistoryOperation::Boolean(_) => 7,
        SolidHistoryOperation::Brep(_) => 8,
        SolidHistoryOperation::Fillet(_) => 9,
        SolidHistoryOperation::Chamfer(_) => 10,
        SolidHistoryOperation::Sweep(_) => 11,
        SolidHistoryOperation::Extrusion(_) => 12,
        SolidHistoryOperation::Loft(_) => 13,
        SolidHistoryOperation::Revolve(_) => 14,
        _ => panic!(),
    }
}
fn object(d: &CadDocument, h: Handle) -> &DynamicBlockObject {
    let ObjectType::DynamicBlock(o) = &d.objects[&h] else {
        panic!("dynamic")
    };
    o
}
fn object_mut(d: &mut CadDocument, h: Handle) -> &mut DynamicBlockObject {
    let ObjectType::DynamicBlock(o) = d.objects.get_mut(&h).unwrap() else {
        panic!("dynamic")
    };
    o
}
fn root(d: &CadDocument, h: Handle) -> &SolidHistory {
    let DynamicBlockData::SolidHistory(r) = &object(d, h).data else {
        panic!("root")
    };
    r
}
fn op(d: &CadDocument, h: Handle) -> &SolidHistoryOperation {
    let DynamicBlockData::SolidHistoryNode(r) = &object(d, h).data else {
        panic!("node")
    };
    r
}
fn store(d: &CadDocument, dir: &str, label: &str) -> acadrust::Result<()> {
    for binary in [false, true] {
        (if binary {
            DxfWriter::new_binary(d)
        } else {
            DxfWriter::new(d)
        })
        .write_to_file(Path::new(dir).join(format!(
            "{label}-{}.dxf",
            if binary { "binary" } else { "ascii" }
        )))?;
    }
    Ok(())
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut d = CadDocument::new();
        d.version = version;
        let mut mats = vec![];
        for name in ["共享材质", "替换材质"] {
            let mut m = Material::new();
            m.name = name.into();
            m.handle = d.allocate_handle();
            m.owner = d.header.named_objects_dict_handle;
            let h = m.handle;
            if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&m.owner) {
                dict.add_entry(name, h);
            }
            d.objects.insert(h, ObjectType::Material(m));
            mats.push(h);
        }
        let sat = build_box([1., 2., 3.], 4., 5., 6.).to_sat_string();
        let main = d.add_entity(EntityType::Solid3D(Solid3D::from_sat(&sat)))?;
        let tool = d.add_entity(EntityType::Solid3D(Solid3D::from_sat(&sat)))?;
        d.create_solid_history(main, operation(0)).unwrap();
        d.append_solid_history(main, operation(9)).unwrap();
        d.create_solid_history(tool, operation(2)).unwrap();
        d.merge_solid_history_boolean(main, tool, SolidHistoryBoolean::SUBTRACT)
            .unwrap();
        d.remove_entity(tool);
        let neighbor = d.add_paper_space_entity(EntityType::Solid3D(Solid3D::from_sat(&sat)))?;
        d.create_solid_history(neighbor, operation(3)).unwrap();
        let mut surf = Surface::new(SurfaceKind::Plane);
        surf.acis_data = AcisData::from_sat(&sat);
        let surface = d.add_entity(EntityType::Surface(Box::new(surf)))?;
        d.create_solid_history(surface, operation(0)).unwrap();
        for k in 1..15 {
            d.append_solid_history(surface, operation(k)).unwrap();
        }
        for e in [main, neighbor, surface] {
            let g = d.solid_history_graph(e).unwrap();
            for (index, h) in g.nodes.iter().enumerate() {
                let n = object_mut(&mut d, *h);
                let DynamicBlockData::SolidHistoryNode(op) = &mut n.data else {
                    panic!()
                };
                let b = op.base_mut().unwrap();
                b.material = mats[0];
                b.transform = std::array::from_fn(|i| {
                    if i % 5 == 0 {
                        1.
                    } else {
                        (i as f64 + 1.) / 10.
                    }
                });
                b.color = if index % 2 == 0 {
                    Color::from_index(3)
                } else {
                    Color::from_rgb(12, 34, 56)
                };
                b.step_id = 100 - index as i32;
                b.eval.value_code = match index % 5 {
                    0 => 40,
                    1 => 10,
                    2 => 90,
                    3 => 70,
                    _ => 1,
                };
                b.eval.value = match index % 5 {
                    0 => BlockEvalValue::Real(1.25),
                    1 => BlockEvalValue::Point([2., 3.]),
                    2 => BlockEvalValue::Long(17),
                    3 => BlockEvalValue::Short(-4),
                    _ => BlockEvalValue::Text("历史".into()),
                };
            }
            // Shared soft material references and a private reactor do not own the material.
            let first = g.nodes[0];
            object_mut(&mut d, first).reactors = vec![mats[0]];
        }
        store(&d, dir, &format!("{version:?}"))?;
        if version == DxfVersion::AC1032 {
            for case in [
                "cycle",
                "missing",
                "duplicate",
                "legacy",
                "fallback",
                "foreign",
            ] {
                let mut bad = d.clone();
                let g = bad.solid_history_graph(main).unwrap();
                let active = bad
                    .solid_history_operation(main)
                    .unwrap()
                    .base()
                    .unwrap()
                    .node_id();
                match case {
                    "cycle" => {
                        let DynamicBlockData::EvaluationGraph(ev) =
                            &mut object_mut(&mut bad, g.evaluation_graph.unwrap()).data
                        else {
                            panic!()
                        };
                        let active_id = ev
                            .nodes
                            .iter()
                            .find(|n| n.node_data[0] >= 0 && n.next_id == ev.first_node_id)
                            .map(|n| n.id)
                            .unwrap();
                        let edge = ev
                            .edges
                            .iter_mut()
                            .find(|e| e.destination_node == active_id)
                            .unwrap();
                        edge.source_node = active_id;
                    }
                    "missing" => {
                        let DynamicBlockData::EvaluationGraph(ev) =
                            &mut object_mut(&mut bad, g.evaluation_graph.unwrap()).data
                        else {
                            panic!()
                        };
                        for n in &mut ev.nodes {
                            if n.node_data[0] >= 0 {
                                n.node_data[0] = 999999;
                            }
                        }
                    }
                    "duplicate" => {
                        let other = *g
                            .nodes
                            .iter()
                            .find(|h| {
                                let DynamicBlockData::SolidHistoryNode(o) = &object(&bad, **h).data
                                else {
                                    panic!()
                                };
                                o.base().unwrap().node_id() != active
                            })
                            .unwrap();
                        let DynamicBlockData::SolidHistoryNode(o) =
                            &mut object_mut(&mut bad, other).data
                        else {
                            panic!()
                        };
                        o.base_mut().unwrap().eval.node_id = active;
                    }
                    "legacy" => {
                        for h in &g.nodes {
                            object_mut(&mut bad, *h).owner = g.root;
                        }
                        bad.objects.remove(&g.evaluation_graph.unwrap());
                        let DynamicBlockData::SolidHistory(r) =
                            &mut object_mut(&mut bad, g.root).data
                        else {
                            panic!()
                        };
                        r.owner = Handle::NULL;
                    }
                    "fallback" => {
                        for h in &g.nodes {
                            let DynamicBlockData::SolidHistoryNode(o) =
                                &mut object_mut(&mut bad, *h).data
                            else {
                                panic!()
                            };
                            let b = o.base_mut().unwrap();
                            if b.node_id() == active {
                                b.eval.node_id = 0;
                                let step = b.step_id;
                                let DynamicBlockData::SolidHistory(r) =
                                    &mut object_mut(&mut bad, g.root).data
                                else {
                                    panic!()
                                };
                                r.history_node_id = step;
                                break;
                            }
                        }
                    }
                    "foreign" => {
                        object_mut(&mut bad, g.evaluation_graph.unwrap()).owner = neighbor;
                    }
                    _ => {}
                }
                store(&bad, dir, &format!("bad-{case}"))?;
            }
        }
    }
    Ok(())
}
fn numbers(out: &mut String, name: &str, values: impl IntoIterator<Item = f64>) {
    out.push_str(name);
    for x in values {
        out.push_str(&format!(" {x:.17e}"));
    }
    out.push('\n');
}
fn trace_op(out: &mut String, name: &str, o: &SolidHistoryOperation) {
    let b = o.base().unwrap();
    numbers(out, name, [kind(o) as f64, b.node_id() as f64]);
}
fn tree(out: &mut String, t: &SolidHistoryTree) {
    trace_op(out, "tree", &t.operation);
    numbers(out, "children", [t.operands.len() as f64]);
    for c in &t.operands {
        tree(out, c);
    }
}
fn trace(d: &CadDocument) -> String {
    let mut out = String::new();
    for e in d.entities() {
        let h = e.common().handle;
        numbers(&mut out, "entity", [h.value() as f64]);
        let Some(g) = d.solid_history_graph(h) else {
            out.push_str("graph none\n");
            continue;
        };
        let r = root(d, g.root);
        numbers(
            &mut out,
            "root",
            [
                g.root.value() as f64,
                object(d, g.root).owner.value() as f64,
                r.owner.value() as f64,
                r.major as f64,
                r.minor as f64,
                r.history_node_id as f64,
                r.show_history as i32 as f64,
                r.record_history as i32 as f64,
            ],
        );
        numbers(&mut out, "nodes", g.nodes.iter().map(|h| h.value() as f64));
        numbers(
            &mut out,
            "evaluation",
            [g.evaluation_graph.unwrap_or(Handle::NULL).value() as f64],
        );
        for h in &g.nodes {
            let o = op(d, *h);
            let b = o.base().unwrap();
            numbers(
                &mut out,
                "node",
                [
                    h.value() as f64,
                    object(d, *h).owner.value() as f64,
                    kind(o) as f64,
                    b.major as f64,
                    b.minor as f64,
                    b.step_id as f64,
                    b.eval.node_id as f64,
                    b.eval.major as f64,
                    b.eval.minor as f64,
                    b.eval.parent_id as f64,
                    b.eval.value_code as f64,
                    b.material.value() as f64,
                ],
            );
            numbers(&mut out, "matrix", b.transform);
            numbers(
                &mut out,
                "color",
                [
                    b.color
                        .to_true_color_value()
                        .map(|v| v as f64)
                        .unwrap_or(-1.),
                    b.color.index().map(|v| v as f64).unwrap_or(-1.),
                ],
            );
            match &b.eval.value {
                BlockEvalValue::None => numbers(&mut out, "value", [0.]),
                BlockEvalValue::Real(v) => numbers(&mut out, "value", [1., *v]),
                BlockEvalValue::Text(v) => out.push_str(&format!("value 2 {v}\n")),
                BlockEvalValue::Long(v) => numbers(&mut out, "value", [3., *v as f64]),
                BlockEvalValue::Handle(v) => numbers(&mut out, "value", [4., v.value() as f64]),
                BlockEvalValue::Short(v) => numbers(&mut out, "value", [5., *v as f64]),
                BlockEvalValue::Point(v) => numbers(&mut out, "value", [6., v[0], v[1]]),
            }
            if let SolidHistoryOperation::Boolean(v) = o {
                numbers(
                    &mut out,
                    "boolean",
                    [
                        v.operation as f64,
                        v.first_operand as f64,
                        v.second_operand as f64,
                    ],
                );
            }
        }
        if let Some(o) = d.solid_history_operation(h) {
            trace_op(&mut out, "active", o);
        } else {
            out.push_str("active none\n");
        }
        if let Some(ops) = d.solid_history_operations(h) {
            numbers(&mut out, "chain", [ops.len() as f64]);
            for o in ops {
                trace_op(&mut out, "step", &o);
            }
        } else {
            out.push_str("chain none\n");
        }
        if let Some(t) = d.solid_history_tree(h) {
            tree(&mut out, &t);
        } else {
            out.push_str("tree none\n");
        }
        let mut refs = vec![];
        for id in std::iter::once(g.root)
            .chain(g.evaluation_graph)
            .chain(g.nodes)
        {
            let mut o = object(d, id).clone();
            o.visit_handles_mut(&mut |target| {
                if !target.is_null() {
                    refs.push((id.value(), target.value()));
                }
            });
        }
        if let Some(xr) = d.xrecord(h, "CADCODEC_SURFACE_HISTORY_V1") {
            let ObjectType::Dictionary(dict) = &d.objects[&xr.owner] else {
                panic!()
            };
            for target in std::iter::once(dict.owner)
                .chain(dict.reactors.iter().copied())
                .chain(dict.xdictionary_handle)
                .chain(dict.entries.iter().map(|(_, h)| *h))
            {
                if !target.is_null() {
                    refs.push((dict.handle.value(), target.value()));
                }
            }
            for target in std::iter::once(xr.owner)
                .chain(xr.reactors.iter().copied())
                .chain(xr.xdictionary_handle)
                .chain(xr.get_references())
            {
                if !target.is_null() {
                    refs.push((xr.handle.value(), target.value()));
                }
            }
        }
        refs.sort();
        for (a, b) in refs {
            numbers(&mut out, "reference", [a as f64, b as f64]);
        }
    }
    out
}
fn compare_trace(a: &str, b: &str) {
    let a: Vec<_> = a.split_whitespace().collect();
    let b: Vec<_> = b.split_whitespace().collect();
    assert_eq!(a.len(), b.len(), "trace sizes");
    for (i, (a, b)) in a.iter().zip(&b).enumerate() {
        match (a.parse::<f64>(), b.parse::<f64>()) {
            (Ok(a), Ok(b)) => assert!(
                (a - b).abs() < 1e-11 * a.abs().max(b.abs()).max(1.),
                "history token {i}: {a} != {b}"
            ),
            _ => assert_eq!(a, b, "history token {i}"),
        }
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let d = DxfReader::from_file(input)?.read()?;
    fs::write(format!("{prefix}.rust-trace"), trace(&d))?;
    compare_trace(&fs::read_to_string(format!("{prefix}.trace"))?, &trace(&d));
    if input.contains("bad-") {
        let h = d
            .entities()
            .find(|e| matches!(e, EntityType::Solid3D(_)))
            .unwrap()
            .common()
            .handle;
        if input.contains("bad-legacy") {
            assert!(d.solid_history_graph(h).unwrap().evaluation_graph.is_none());
            assert_eq!(d.solid_history_operations(h).unwrap().len(), 1);
        } else if !input.contains("bad-fallback") {
            assert!(
                d.solid_history_tree(h).is_none(),
                "malformed fixture must exercise failure"
            );
        }
        println!("Malformed/legacy history query matches Rust");
        return Ok(());
    }
    let manifest = fs::read_to_string(format!("{prefix}.manifest"))?;
    let handles: Vec<_> = manifest
        .split_whitespace()
        .map(|v| Handle::new(u64::from_str_radix(v, 16).unwrap()))
        .collect();
    let (source, copy, material) = (handles[0], handles[1], handles[2]);
    for encoding in ["ascii", "binary"] {
        let out = DxfReader::from_file(format!("{prefix}.copy.{encoding}.dxf"))?.read()?;
        let original = d.solid_history_tree(source).unwrap();
        let mut expected = original.clone();
        fn set_material(t: &mut SolidHistoryTree, h: Handle) {
            t.operation.base_mut().unwrap().material = h;
            for c in &mut t.operands {
                set_material(c, h);
            }
        }
        set_material(&mut expected, material);
        assert_eq!(out.solid_history_tree(copy).unwrap(), expected);
        assert_eq!(out.solid_history_tree(source).unwrap(), original);
        let cg = out.solid_history_graph(copy).unwrap();
        let sg = d.solid_history_graph(source).unwrap();
        assert_ne!(cg.root, sg.root);
        assert_eq!(object(&out, cg.root).owner, copy);
        for h in &cg.nodes {
            assert!(!sg.nodes.contains(h));
            assert!(out.owner_chain_reaches(object(&out, *h).owner, cg.root));
        }
        for e in d.entities() {
            assert_eq!(out.get_entity(e.common().handle).unwrap(), e);
            assert_eq!(
                out.solid_history_tree(e.common().handle),
                d.solid_history_tree(e.common().handle)
            );
        }
        let deleted = DxfReader::from_file(format!("{prefix}.deleted.{encoding}.dxf"))?.read()?;
        assert!(deleted.get_entity(copy).is_none());
        assert!(!deleted.objects.contains_key(&cg.root));
        for h in cg.nodes {
            assert!(!deleted.objects.contains_key(&h));
        }
        for e in d.entities() {
            assert_eq!(deleted.get_entity(e.common().handle).unwrap(), e);
            assert_eq!(
                deleted.solid_history_tree(e.common().handle),
                d.solid_history_tree(e.common().handle)
            );
        }
        assert_eq!(deleted.objects.len(), d.objects.len());
        let surface = handles[4];
        let surface_copy = handles[5];
        let surface_out =
            DxfReader::from_file(format!("{prefix}.surface-copy.{encoding}.dxf"))?.read()?;
        assert_eq!(
            surface_out.solid_history_tree(surface_copy),
            d.solid_history_tree(surface)
        );
        assert_eq!(
            surface_out
                .solid_history_graph(surface_copy)
                .unwrap()
                .nodes
                .len(),
            15
        );
        for (src, dst, doc, changed_material) in [
            (source, copy, &out, true),
            (surface, surface_copy, &surface_out, false),
        ] {
            let a = d.solid_history_graph(src).unwrap();
            let b = doc.solid_history_graph(dst).unwrap();
            let mut remap = std::collections::HashMap::from([
                (src, dst),
                (a.root, b.root),
                (a.evaluation_graph.unwrap(), b.evaluation_graph.unwrap()),
            ]);
            for (x, y) in a.nodes.iter().zip(&b.nodes) {
                remap.insert(*x, *y);
            }
            for x in std::iter::once(a.root)
                .chain(a.evaluation_graph)
                .chain(a.nodes)
            {
                let mut expected = object(&d, x).clone();
                expected.handle = remap[&x];
                expected.visit_handles_mut(&mut |h| {
                    if let Some(new) = remap.get(h) {
                        *h = *new;
                    }
                });
                if changed_material {
                    if let DynamicBlockData::SolidHistoryNode(op) = &mut expected.data {
                        op.base_mut().unwrap().material = material;
                    }
                }
                assert_eq!(
                    object(doc, remap[&x]),
                    &expected,
                    "history payload/reference remap"
                );
            }
        }
        let surface_deleted =
            DxfReader::from_file(format!("{prefix}.surface-deleted.{encoding}.dxf"))?.read()?;
        assert!(surface_deleted.get_entity(surface_copy).is_none());
        assert_eq!(surface_deleted.objects.len(), d.objects.len());
        for e in d.entities() {
            assert_eq!(surface_out.get_entity(e.common().handle).unwrap(), e);
            assert_eq!(surface_deleted.get_entity(e.common().handle).unwrap(), e);
            assert_eq!(
                surface_deleted.solid_history_tree(e.common().handle),
                d.solid_history_tree(e.common().handle)
            );
        }
        let active = DxfReader::from_file(format!("{prefix}.active.{encoding}.dxf"))?.read()?;
        let changed = active.solid_history_operation(source).unwrap();
        assert_eq!(changed.base().unwrap().node_id(), handles[3].value() as i32);
        let reordered =
            DxfReader::from_file(format!("{prefix}.reordered.{encoding}.dxf"))?.read()?;
        assert_eq!(
            reordered.solid_history_tree(source),
            d.solid_history_tree(source)
        );
    }
    println!(
        "History graph, tree, references, active edits, graph order and copy/delete match Rust"
    );
    Ok(())
}
