// SPDX-License-Identifier: MPL-2.0
use acadrust::{
    entities::Insert, objects::*, tables::BlockRecord, CadDocument, DxfReader, DxfWriter,
    EntityType, Handle, Vector3,
};
use std::fmt::Write;
fn block(d: &mut CadDocument, name: &str) -> Handle {
    let mut b = BlockRecord::new(name);
    b.handle = d.allocate_handle();
    b.block_entity_handle = d.allocate_handle();
    b.block_end_handle = d.allocate_handle();
    let h = b.handle;
    d.block_records.add(b).unwrap();
    h
}
fn entry(d: &mut CadDocument, parent: Handle, key: &str, h: Handle) {
    let ObjectType::Dictionary(dict) = d.objects.get_mut(&parent).unwrap() else {
        panic!()
    };
    dict.add_entry(key, h);
}
fn dict(d: &mut CadDocument, parent: Handle, key: &str) -> Handle {
    let h = d.allocate_handle();
    let mut v = Dictionary::new();
    v.handle = h;
    v.owner = parent;
    v.hard_owner = true;
    d.objects.insert(h, ObjectType::Dictionary(v));
    entry(d, parent, key, h);
    h
}
fn object(
    d: &mut CadDocument,
    owner: Handle,
    name: &str,
    scope: &str,
    data: DynamicBlockData,
) -> Handle {
    let h = d.allocate_handle();
    let mut o = DynamicBlockObject::new(name, scope);
    o.handle = h;
    o.owner = owner;
    o.data = data;
    d.objects.insert(h, ObjectType::DynamicBlock(o));
    h
}
fn parameter(id: i32) -> BlockParameter {
    let mut p = BlockParameter::default();
    p.element.name = format!("参数{id}");
    p.element.eval.node_id = id;
    p.element.eval.value_code = -9999;
    p.show_properties = id % 2 == 0;
    p
}
fn two(id: i32) -> BlockTwoPointParameter {
    BlockTwoPointParameter {
        parameter: parameter(id),
        definition_base_point: Vector3::new(1., 2., 3.),
        definition_end_point: Vector3::new(4., 6., 15.),
        ..Default::default()
    }
}
fn one(id: i32) -> BlockOnePointParameter {
    BlockOnePointParameter {
        parameter: parameter(id),
        definition_point: Vector3::new(-2., 7., 4.),
        ..Default::default()
    }
}
fn choices() -> BlockParameterValueSet {
    BlockParameterValueSet {
        values: vec![2., 5., 13.],
        minimum: 1.,
        maximum: 20.,
        increment: 0.5,
        ..Default::default()
    }
}
fn graph(
    d: &mut CadDocument,
    b: Handle,
    items: Vec<(String, String, DynamicBlockData)>,
) -> Vec<Handle> {
    let dict = d.ensure_extension_dictionary(b);
    let h = d.allocate_handle();
    let nodes: Vec<_> = items
        .into_iter()
        .enumerate()
        .map(|(i, (name, scope, data))| {
            let expr = object(d, h, &name, &scope, data);
            BlockEvaluationNode {
                id: i as i32 + 1,
                expression: expr,
                node_data: [1, 2, 3, 4],
                next_id: -1,
                ..Default::default()
            }
        })
        .collect();
    let handles = nodes.iter().map(|n| n.expression).collect();
    let mut obj = DynamicBlockObject::new("ACAD_EVALUATION_GRAPH", "AcDbEvalGraph");
    obj.handle = h;
    obj.owner = dict;
    obj.data = DynamicBlockData::EvaluationGraph(BlockEvaluationGraph {
        first_node_id: 1,
        first_node_id_copy: 1,
        nodes,
        edges: vec![BlockEvaluationEdge {
            id: 17,
            next_id: -1,
            incoming_edge: 2,
            source_node: 1,
            destination_node: 2,
            outgoing_edges: [3, 4, 5, 6, 7],
        }],
    });
    d.objects.insert(h, ObjectType::DynamicBlock(obj));
    entry(d, dict, "ACAD_ENHANCEDBLOCK", h);
    handles
}
fn item(name: &str, scope: &str, data: DynamicBlockData) -> (String, String, DynamicBlockData) {
    (name.into(), scope.into(), data)
}
fn record(d: &mut CadDocument, parent: Handle, node: i32, mut r: XRecord) {
    r.handle = d.allocate_handle();
    r.owner = parent;
    let h = r.handle;
    d.objects.insert(h, ObjectType::XRecord(r));
    entry(d, parent, &node.to_string(), h);
}
fn cache(d: &mut CadDocument, insert: Handle, malformed: bool) {
    let top = d.ensure_extension_dictionary(insert);
    let r = dict(d, top, "AcDbBlockRepresentation");
    let a = dict(d, r, "AppDataCache");
    let data = dict(d, a, "ACAD_ENHANCEDBLOCKDATA");
    for id in [1, 2, 3, 4, 5, 10, 11, 12, 13] {
        let mut r = XRecord::new();
        r.add_point3d(10, 2., 3., 4.);
        if !malformed {
            r.add_point3d(11, -4., 11., 4.);
            r.add_point3d(12, 0., 0., 0.);
            r.add_point3d(13, 3., 3., 4.);
        }
        record(d, data, id, r);
    }
    for (id, text) in [(7, "大号"), (8, "局部")] {
        let mut r = XRecord::new();
        r.add_string(1, text);
        record(d, data, id, r);
    }
    let mut r = XRecord::new();
    for i in 0..4 {
        match i {
            0 => r.add_string(102, "CACHE_HEADER"),
            1 => r.add_string(100, "literal cache data"),
            _ => r.add_int32(90, i),
        }
    }
    r.add_int16(70, 1);
    record(d, data, 6, r);
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    use DynamicBlockData as D;
    let mut d = CadDocument::new();
    let b = block(&mut d, "Dyn主块");
    let mut items = vec![
        item(
            "BLOCKLINEARPARAMETER",
            "AcDbBlockLinearParameter",
            D::LinearParameter(BlockLinearParameter {
                parameter: two(1),
                distance_name: "长度".into(),
                distance_description: "三维长度".into(),
                value_set: choices(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKPOLARPARAMETER",
            "AcDbBlockPolarParameter",
            D::PolarParameter(BlockPolarParameter {
                parameter: two(2),
                distance_name: "极距".into(),
                angle_name: "极角".into(),
                distance_value_set: choices(),
                angle_value_set: choices(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKROTATIONPARAMETER",
            "AcDbBlockRotationParameter",
            D::RotationParameter(BlockRotationParameter {
                parameter: two(3),
                angle_name: "旋转".into(),
                definition_base_angle_point: Vector3::new(2., 2., 3.),
                value_set: choices(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKXYPARAMETER",
            "AcDbBlockXYParameter",
            D::XYParameter(BlockXYParameter {
                parameter: two(4),
                x_label: "横距".into(),
                y_label: "纵距".into(),
                x_value_set: choices(),
                y_value_set: choices(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKPOINTPARAMETER",
            "AcDbBlockPointParameter",
            D::PointParameter(BlockPointParameter {
                parameter: one(5),
                position_name: "位置".into(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKFLIPPARAMETER",
            "AcDbBlockFlipParameter",
            D::FlipParameter(BlockFlipParameter {
                parameter: two(6),
                flip_label: "翻转".into(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKLOOKUPPARAMETER",
            "AcDbBlockLookupParameter",
            D::LookupParameter(BlockLookupParameter {
                parameter: one(7),
                index: 70,
                lookup_name: "规格".into(),
                ..Default::default()
            }),
        ),
        item(
            "BLOCKVISIBILITYPARAMETER",
            "AcDbBlockVisibilityParameter",
            D::VisibilityParameter(BlockVisibilityParameter {
                eval_node_id: 8,
                eval_value_code: -9999,
                name: "显示".into(),
                show_properties: true,
                states: vec![
                    BlockVisibilityState {
                        name: "全部".into(),
                        ..Default::default()
                    },
                    BlockVisibilityState {
                        name: "局部".into(),
                        ..Default::default()
                    },
                ],
                ..Default::default()
            }),
        ),
        item(
            "BLOCKUSERPARAMETER",
            "AcDbBlockUserParameter",
            D::UserParameter(BlockUserParameter {
                parameter: one(9),
                value_code: 40,
                value: BlockEvalValue::Real(7.25),
                value_type: 3,
                ..Default::default()
            }),
        ),
    ];
    for (id, name, variant) in [
        (10, "BLOCKALIGNEDCONSTRAINTPARAMETER", 0),
        (11, "BLOCKLINEARCONSTRAINTPARAMETER", 1),
        (12, "BLOCKHORIZONTALCONSTRAINTPARAMETER", 2),
        (13, "BLOCKVERTICALCONSTRAINTPARAMETER", 3),
    ] {
        let p = BlockLinearConstraintParameter {
            constraint: BlockConstraintParameter {
                parameter: two(id),
                ..Default::default()
            },
            expression_name: format!("约束{id}"),
            value_set: choices(),
            ..Default::default()
        };
        items.push(item(
            name,
            "AcDbBlockLinearConstraintParameter",
            match variant {
                0 => D::AlignedConstraintParameter(p),
                1 => D::LinearConstraintParameter(p),
                2 => D::HorizontalConstraintParameter(p),
                _ => D::VerticalConstraintParameter(p),
            },
        ));
    }
    let mut lookup = BlockLookupAction::default();
    lookup.action.element.eval.node_id = 70;
    lookup.action.element.eval.value_code = -9999;
    lookup.row_count = 3;
    lookup.column_count = 2;
    lookup.expressions = vec![
        "小号".into(),
        "2".into(),
        "大号".into(),
        "5".into(),
        "大号".into(),
        "8".into(),
    ];
    lookup.columns = vec![
        BlockLookupColumn {
            node_id: 7,
            lookup_property: true,
            writable: false,
            unmatched_name: "自定义".into(),
            ..Default::default()
        },
        BlockLookupColumn {
            node_id: 1,
            lookup_property: false,
            ..Default::default()
        },
    ];
    items.push(item(
        "BLOCKLOOKUPACTION",
        "AcDbBlockLookupAction",
        D::LookupAction(lookup),
    ));
    let mut flip = BlockFlipAction::default();
    flip.action.element.eval.node_id = 71;
    flip.action.element.eval.value_code = -9999;
    flip.connections[0].code = 6;
    items.push(item(
        "BLOCKFLIPACTION",
        "AcDbBlockFlipAction",
        D::FlipAction(flip),
    ));
    items.push(item(
        "BLOCKALIGNMENTPARAMETER",
        "AcDbBlockAlignmentParameter",
        D::AlignmentParameter(BlockAlignmentParameter {
            parameter: two(15),
            ..Default::default()
        }),
    ));
    items.push(item(
        "BLOCKBASEPOINTPARAMETER",
        "AcDbBlockBasepointParameter",
        D::BasePointParameter(BlockBasePointParameter {
            parameter: one(16),
            ..Default::default()
        }),
    ));
    for (id, value, code) in [
        (17, BlockEvalValue::Text("中文用户值".into()), 1),
        (18, BlockEvalValue::Long(1234), 90),
        (19, BlockEvalValue::Short(12), 70),
        (20, BlockEvalValue::None, -9999),
    ] {
        items.push(item(
            "BLOCKUSERPARAMETER",
            "AcDbBlockUserParameter",
            D::UserParameter(BlockUserParameter {
                parameter: one(id),
                flags: 23,
                value_code: code,
                value,
                value_type: 0,
                ..Default::default()
            }),
        ));
    }
    items.push(item(
        "ACDB_DYNAMICBLOCKPROXYNODE",
        "AcDbDynamicBlockProxyNode",
        D::ProxyNode(BlockEvalExpression {
            node_id: 90,
            value_code: -9999,
            ..Default::default()
        }),
    ));
    items.push(item(
        "ACDBBLOCKPARAMDEPENDENCYBODY",
        "AcDbBlockParameterDependencyBody",
        D::ParameterDependencyBody(BlockParameterDependencyBody::default()),
    ));
    let hs = graph(&mut d, b, items);
    if let ObjectType::DynamicBlock(o) = d.objects.get_mut(&hs[7]).unwrap() {
        if let D::VisibilityParameter(p) = &mut o.data {
            p.handle = hs[7];
            p.owner = o.owner;
            p.states[0].visible_params = hs.clone();
            p.states[1].visible_params = vec![hs[0], hs[6]];
        }
    }
    if let ObjectType::DynamicBlock(o) = d.objects.get_mut(&hs[14]).unwrap() {
        if let D::FlipAction(p) = &mut o.data {
            p.action.dependencies = vec![hs[2]];
        }
    }
    for (sx, sy, sz, saved, bad) in [
        (1., 1., 1., false, false),
        (1., 1., 1., true, false),
        (-2., 2., -2., true, false),
        (2., 3., 2., true, false),
        (0., 0., 0., false, false),
        (1., 1., 1., true, true),
    ] {
        let i = d.add_entity(EntityType::Insert(
            Insert::new("Dyn主块", Vector3::ZERO).with_scale(sx, sy, sz),
        ))?;
        if saved {
            cache(&mut d, i, bad);
        }
    }
    let anon = block(&mut d, "*U1");
    let i = d.add_paper_space_entity(EntityType::Insert(Insert::new("*U1", Vector3::ZERO)))?;
    let dict = d.ensure_extension_dictionary(i);
    let rep = object(
        &mut d,
        dict,
        "ACDB_BLOCKREPRESENTATION_DATA",
        "AcDbBlockRepresentationData",
        D::Representation(BlockRepresentationData { flags: 1, block: b }),
    );
    entry(&mut d, dict, "Representation", rep);
    cache(&mut d, i, false);
    assert_ne!(anon, b);
    let unsupported = block(&mut d, "DynUnsupported");
    graph(
        &mut d,
        unsupported,
        vec![item(
            "BLOCKANGULARCONSTRAINTPARAMETER",
            "AcDbBlockAngularConstraintParameter",
            D::AngularConstraintParameter(BlockAngularConstraintParameter::default()),
        )],
    );
    let missing = block(&mut d, "DynMissing");
    let ms = graph(
        &mut d,
        missing,
        vec![item(
            "BLOCKPOINTPARAMETER",
            "AcDbBlockPointParameter",
            D::PointParameter(BlockPointParameter::default()),
        )],
    );
    d.objects.remove(&ms[0]);
    let invalid = block(&mut d, "DynLookupMissing");
    graph(
        &mut d,
        invalid,
        vec![item(
            "BLOCKLOOKUPPARAMETER",
            "AcDbBlockLookupParameter",
            D::LookupParameter(BlockLookupParameter {
                parameter: one(1),
                index: 999,
                ..Default::default()
            }),
        )],
    );
    block(&mut d, "Ordinary");
    DxfWriter::new(&d).write_to_file(ascii)?;
    // Dynamic writer uses write_i32 for i16 group 170/171; the group-aware
    // writer produces valid binary group widths from the exact ASCII payload.
    use acadrust::io::dxf::{DxfBinaryWriter, DxfStreamWriter};
    let text = std::fs::read_to_string(ascii)?;
    let mut lines = text.lines();
    let mut out = DxfBinaryWriter::new(std::fs::File::create(binary)?)?;
    while let Some(code) = lines.next() {
        out.write_string(code.trim().parse().unwrap(), lines.next().unwrap())?;
    }
    Ok(())
}
fn text(s: &str) -> String {
    if s.is_empty() {
        return "-".into();
    }
    s.as_bytes().iter().map(|b| format!("{b:02X}")).collect()
}
fn value(v: &BlockEvalValue) -> String {
    match v {
        BlockEvalValue::None => "N".into(),
        BlockEvalValue::Real(v) => format!("R{v:.17e}"),
        BlockEvalValue::Text(v) => format!("S{}", text(v)),
        BlockEvalValue::Long(v) => format!("I{v}"),
        BlockEvalValue::Short(v) => format!("J{v}"),
        BlockEvalValue::Handle(v) => format!("H{:X}", v.value()),
        BlockEvalValue::Point(v) => format!("P{} {}", v[0], v[1]),
    }
}
fn trace(d: &CadDocument) -> String {
    let mut out = String::new();
    let mut blocks: Vec<_> = d.block_records.iter().collect();
    blocks.sort_by_key(|b| b.handle.value());
    let mut inserts: Vec<_> = d
        .entities()
        .filter_map(|e| {
            if let EntityType::Insert(i) = e {
                Some(i.common.handle)
            } else {
                None
            }
        })
        .collect();
    inserts.sort_by_key(|h| h.value());
    for i in &inserts {
        let resolved = d
            .dynamic_definition_for_insert(*i)
            .map(|h| h.value())
            .unwrap_or(0);
        writeln!(out, "I\t{:X}\t{resolved:X}", i.value()).unwrap();
    }
    for b in blocks {
        writeln!(
            out,
            "B\t{:X}\t{}\t{:X}",
            b.handle.value(),
            d.is_dynamic_block(b.handle) as u8,
            d.dynamic_block_definition(b.handle)
                .map(|h| h.value())
                .unwrap_or(0)
        )
        .unwrap();
        for insert in std::iter::once(None).chain(inserts.iter().copied().map(Some)) {
            let h = insert.map(|h| h.value()).unwrap_or(0);
            match d.dynamic_block_properties(b.handle, insert) {
                Err(_) => writeln!(out, "Q\t{:X}\t{h:X}\tERR", b.handle.value()).unwrap(),
                Ok(props) => {
                    writeln!(out, "Q\t{:X}\t{h:X}\t{}", b.handle.value(), props.len()).unwrap();
                    for p in props {
                        write!(
                            out,
                            "P\t{:X}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}",
                            p.parameter_handle.value(),
                            text(&p.name),
                            text(&p.description),
                            p.type_code,
                            p.units,
                            p.show as u8,
                            p.read_only as u8,
                            p.visible as u8,
                            value(&p.value),
                            p.allowed_values.len()
                        )
                        .unwrap();
                        for v in &p.allowed_values {
                            write!(out, "\t{}", value(v)).unwrap();
                        }
                        out.push('\n');
                    }
                }
            }
        }
    }
    out
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let expected = trace(&source);
    let actual = std::fs::read_to_string(format!("{prefix}.trace"))?;
    let a: Vec<_> = actual.lines().collect();
    let b: Vec<_> = expected.lines().collect();
    assert_eq!(a.len(), b.len(), "trace line counts");
    for (row, (a, b)) in a.iter().zip(&b).enumerate() {
        let aa: Vec<_> = a.split('\t').collect();
        let bb: Vec<_> = b.split('\t').collect();
        assert_eq!(aa.len(), bb.len(), "line {row}: {a} vs {b}");
        for (a, b) in aa.iter().zip(bb) {
            if a.starts_with('R') && b.starts_with('R') {
                crate::entity_transform::near(a[1..].parse().unwrap(), b[1..].parse().unwrap());
            } else {
                assert_eq!(*a, b, "line {row}");
            }
        }
    }
    for suffix in ["ascii", "binary"] {
        let saved = DxfReader::from_file(format!("{prefix}-{suffix}.dxf"))?.read()?;
        assert_eq!(trace(&saved), expected);
        assert_eq!(source.objects.len(), saved.objects.len());
        for (h, o) in &source.objects {
            let mut a = o.clone();
            let mut b = saved.objects.get(h).unwrap().clone();
            // Binary numeric pair strings omit ASCII column padding. Keep every
            // typed field and every string-valued plot setting in the comparison.
            for o in [&mut a, &mut b] {
                let raw = match o {
                    ObjectType::Layout(l) => &mut l.raw_plot_settings_codes,
                    ObjectType::TableStyle(t) => &mut t.raw_dxf_codes,
                    _ => continue,
                };
                if let Some(raw) = raw {
                    for (code, value) in raw {
                        use acadrust::io::dxf::GroupCodeValueType as T;
                        if matches!(T::from_raw_code(*code), T::Double) {
                            *value = format!("{:.17e}", value.trim().parse::<f64>().unwrap());
                        } else if matches!(
                            T::from_raw_code(*code),
                            T::Byte | T::Int16 | T::Int32 | T::Int64 | T::Bool
                        ) {
                            *value = value.trim().to_owned();
                        }
                    }
                }
            }
            assert_eq!(a, b, "object {:X} changed in {suffix}", h.value());
        }
        assert_eq!(
            source.entities().collect::<Vec<_>>(),
            saved.entities().collect::<Vec<_>>()
        );
    }
    println!(
        "Dynamic properties: {} trace rows, both saved DXF encodings match Rust",
        a.len()
    );
    Ok(())
}
