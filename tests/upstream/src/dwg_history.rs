// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::*;
use acadrust::{DwgReader, DwgWriter, DxfReader, EntityType};
use std::{fs, path::Path};
pub fn generate(source: &str, dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for v in ["AC1021", "AC1024", "AC1027", "AC1032"] {
        let mut d =
            DxfReader::from_file(Path::new(source).join(format!("{v}-ascii.dxf")))?.read()?;
        let profiles = super::history_profiles::profiles();
        for object in d.objects.values_mut() {
            if let ObjectType::DynamicBlock(o) = object {
                if let DynamicBlockData::SolidHistoryNode(op) = &mut o.data {
                    if let SolidHistoryOperation::Sphere(v) = op {
                        v.base.eval.value_code = 91;
                        v.base.eval.value = BlockEvalValue::Handle(acadrust::Handle::new(20));
                    }
                    match op {
                        SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => {
                            v.sweep_entity = Some(profiles[10].clone());
                            v.path_entity = Some(profiles[5].clone());
                        }
                        SolidHistoryOperation::Revolve(v) => {
                            v.sweep_entity = Some(profiles[2].clone())
                        }
                        SolidHistoryOperation::Loft(v) => {
                            v.cross_sections = profiles.clone();
                            v.guides = vec![profiles[8].clone()];
                            v.path_entity = Some(profiles[4].clone());
                        }
                        _ => {}
                    }
                }
            }
        }
        DwgWriter::write_to_file(Path::new(dir).join(format!("{v}.dwg")), &d)?;
        for object in d.objects.values_mut() {
            if let ObjectType::DynamicBlock(o) = object {
                if let DynamicBlockData::SolidHistoryNode(SolidHistoryOperation::Loft(loft)) =
                    &mut o.data
                {
                    loft.parameters = Some(Default::default());
                }
            }
        }
        DwgWriter::write_to_file(Path::new(dir).join(format!("{v}.dwg.metadata.dwg")), &d)?;
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let d = DwgReader::from_file(input)?.read()?;
    let saved = DwgReader::from_file(format!("{prefix}.dwg"))?.read()?;
    assert_eq!(d.objects, saved.objects);
    assert_eq!(
        d.entities().collect::<Vec<_>>(),
        saved.entities().collect::<Vec<_>>()
    );
    let roundtrip = DwgReader::from_file(format!("{prefix}.parameters.dwg"))?.read()?;
    // Embedded fit-point splines normalize their raw flag word on a Rust rewrite.
    DwgWriter::write_to_file(format!("{prefix}.rust-rewritten.dwg"), &d)?;
    let canonical = DwgReader::from_file(format!("{prefix}.rust-rewritten.dwg"))?.read()?;

    let mut meta = vec![];
    for (h, o) in &d.objects {
        let ObjectType::DynamicBlock(o) = o else {
            continue;
        };
        if !matches!(
            o.data,
            DynamicBlockData::SolidHistory(_)
                | DynamicBlockData::SolidHistoryNode(_)
                | DynamicBlockData::EvaluationGraph(_)
        ) {
            continue;
        }
        let mut row = format!(
            "{} {} {}",
            h.value(),
            o.xdictionary_handle.unwrap_or_default().value(),
            o.reactors.len()
        );
        for x in &o.reactors {
            row += &format!(" {}", x.value());
        }
        if let DynamicBlockData::SolidHistoryNode(op) = &o.data {
            let b = op.base().unwrap();
            let mut version = (0, 0);
            macro_rules! fields {
                ($v:expr) => {
                    version = ($v.operation_major, $v.operation_minor)
                };
            }
            match op {
                SolidHistoryOperation::Box(v) | SolidHistoryOperation::Wedge(v) => fields!(v),
                SolidHistoryOperation::Sphere(v) => fields!(v),
                SolidHistoryOperation::Cylinder(v) => fields!(v),
                SolidHistoryOperation::Cone(v) => fields!(v),
                SolidHistoryOperation::Pyramid(v) => fields!(v),
                SolidHistoryOperation::Torus(v) => fields!(v),
                SolidHistoryOperation::Boolean(v) => fields!(v),
                SolidHistoryOperation::Brep(v) => fields!(v),
                SolidHistoryOperation::Fillet(v) => fields!(v),
                SolidHistoryOperation::Chamfer(v) => fields!(v),
                SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => fields!(v),
                SolidHistoryOperation::Loft(v) => fields!(v),
                SolidHistoryOperation::Revolve(v) => fields!(v),
                _ => {}
            }
            row += &format!(" {} {}", version.0, version.1);
            for x in b.transform {
                row += &format!(" {}", x.to_bits());
            }
            let rgb = b
                .color
                .true_color_rgb()
                .map(|(r, g, b)| ((r as u32) << 16) | ((g as u32) << 8) | (b as u32))
                .unwrap_or(0);
            row += &format!(
                " {} {}",
                b.color.index().map(|x| x as i32).unwrap_or(-1),
                rgb
            );
            match &b.eval.value {
                BlockEvalValue::Real(x) => row += &format!(" {}", x.to_bits()),
                BlockEvalValue::Point(p) => {
                    for x in p {
                        row += &format!(" {}", x.to_bits());
                    }
                }
                BlockEvalValue::Text(s) => assert_eq!(
                    &fs::read_to_string(format!("{prefix}.value-{}", h.value()))?,
                    s
                ),
                BlockEvalValue::Long(x) => row += &format!(" {x}"),
                BlockEvalValue::Short(x) => row += &format!(" {x}"),
                BlockEvalValue::Handle(x) => row += &format!(" {}", x.value()),
                _ => {}
            }
        }
        meta.push(row);
        let ObjectType::DynamicBlock(saved) = roundtrip.objects.get(h).unwrap() else {
            panic!("node")
        };
        let ObjectType::DynamicBlock(canonical_object) = canonical.objects.get(h).unwrap() else {
            panic!("history")
        };
        let mut old = canonical_object.clone();
        let mut got = saved.clone();
        if let (
            DynamicBlockData::SolidHistoryNode(SolidHistoryOperation::Brep(a)),
            DynamicBlockData::SolidHistoryNode(SolidHistoryOperation::Brep(b)),
        ) = (&mut old.data, &mut got.data)
        {
            super::acis::compare(
                &super::acis::canonical(&a.acis_data.parse().unwrap()),
                &super::acis::canonical(&b.acis_data.parse().unwrap()),
            );
            a.acis_data = b.acis_data.clone();
        }
        super::history_author::compare(&old.data, &got.data);
    }
    meta.sort();
    let text = fs::read_to_string(format!("{prefix}.metadata"))?;
    let mut native = text.lines().map(str::to_owned).collect::<Vec<_>>();
    native.sort();
    assert_eq!(native, meta);
    let mut rows = vec![];
    for (h, o) in &d.objects {
        let ObjectType::DynamicBlock(o) = o else {
            continue;
        };
        let mut row = format!("{} {}", h.value(), o.owner.value());
        match &o.data {
            DynamicBlockData::SolidHistory(v) => {
                row += &format!(
                    " root {} {} {} {} {} {}",
                    v.major,
                    v.minor,
                    v.owner.value(),
                    v.history_node_id,
                    v.show_history as u8,
                    v.record_history as u8
                )
            }
            DynamicBlockData::SolidHistoryNode(op) => {
                let b = op.base().unwrap();
                row += &format!(
                    " node {} {} {} {} {} {} {} {} {} {}",
                    b.major,
                    b.minor,
                    b.step_id,
                    b.eval.node_id,
                    b.eval.major,
                    b.eval.minor,
                    b.eval.parent_id,
                    b.eval.value_code,
                    b.material.value(),
                    super::solid_history::kind(op)
                );
            }
            DynamicBlockData::EvaluationGraph(g) => {
                row += &format!(" graph {} {}", g.first_node_id, g.first_node_id_copy);
                for n in &g.nodes {
                    row += &format!(
                        " n {} {} {} {}",
                        n.id,
                        n.edge_flags,
                        n.next_id,
                        n.expression.value()
                    );
                    for x in n.node_data {
                        row += &format!(" {x}");
                    }
                }
                for e in &g.edges {
                    row += &format!(
                        " e {} {} {} {} {}",
                        e.id, e.next_id, e.incoming_edge, e.source_node, e.destination_node
                    );
                    for x in e.outgoing_edges {
                        row += &format!(" {x}");
                    }
                }
            }
            _ => continue,
        }
        rows.push(row);
    }
    rows.sort();
    let text = fs::read_to_string(format!("{prefix}.txt"))?;
    let mut cpp = text.lines().map(str::to_owned).collect::<Vec<_>>();
    cpp.sort();
    assert_eq!(cpp, rows);
    let graph = fs::read_to_string(format!("{prefix}.graphs"))?;
    let mut expected = String::new();
    let mut entities = d
        .entities()
        .filter(|e| matches!(e, EntityType::Solid3D(_) | EntityType::Surface(_)))
        .collect::<Vec<_>>();
    entities.sort_by_key(|e| e.common().handle);
    for e in entities {
        if let Some(g) = d.solid_history_graph(e.common().handle) {
            expected += &format!(
                "{} {} {}",
                e.common().handle.value(),
                g.root.value(),
                g.evaluation_graph.unwrap_or_default().value()
            );
            for h in g.nodes {
                expected += &format!(" {}", h.value());
            }
            expected.push('\n');
        }
    }
    assert_eq!(graph, expected);

    let edited = DwgReader::from_file(format!("{prefix}.edited.dwg"))?.read()?;
    assert_eq!(
        d.entities().collect::<Vec<_>>(),
        edited.entities().collect::<Vec<_>>()
    );
    let ids = fs::read_to_string(format!("{prefix}.edited"))?
        .split_whitespace()
        .map(|x| x.parse::<u64>().unwrap())
        .collect::<Vec<_>>();
    for (h, object) in &d.objects {
        let Some(actual) = edited.objects.get(h) else {
            panic!("missing object")
        };
        if !ids.contains(&h.value()) {
            assert_eq!(object, actual);
            continue;
        }
        let ObjectType::DynamicBlock(old) = object else {
            panic!("history")
        };
        let ObjectType::DynamicBlock(got) = actual else {
            panic!("history")
        };
        let DynamicBlockData::SolidHistoryNode(before) = &old.data else {
            panic!("node")
        };
        let DynamicBlockData::SolidHistoryNode(after) = &got.data else {
            panic!("node")
        };
        let mut expected = super::history_author::changed(before.clone());
        *expected.base_mut().unwrap() = before.base().unwrap().clone();
        macro_rules! version {
            ($v:expr) => {{
                $v.operation_major -= 3;
                $v.operation_minor -= 4;
            }};
        }
        let line = acadrust::entities::EmbeddedEntity::Line(acadrust::entities::Line::from_points(
            acadrust::Vector3::new(3., 4., 5.),
            acadrust::Vector3::new(7., 8., 9.),
        ));
        match &mut expected {
            SolidHistoryOperation::Box(v) | SolidHistoryOperation::Wedge(v) => version!(v),
            SolidHistoryOperation::Sphere(v) => version!(v),
            SolidHistoryOperation::Cylinder(v) => version!(v),
            SolidHistoryOperation::Cone(v) => version!(v),
            SolidHistoryOperation::Pyramid(v) => version!(v),
            SolidHistoryOperation::Torus(v) => version!(v),
            SolidHistoryOperation::Boolean(v) => version!(v),
            SolidHistoryOperation::Fillet(v) => version!(v),
            SolidHistoryOperation::Chamfer(v) => version!(v),
            SolidHistoryOperation::Brep(v) => {
                version!(v);
                let SolidHistoryOperation::Brep(a) = after else {
                    panic!("brep")
                };
                super::acis::compare(
                    &super::acis::canonical(&v.acis_data.parse().unwrap()),
                    &super::acis::canonical(&a.acis_data.parse().unwrap()),
                );
                v.acis_data = a.acis_data.clone();
            }
            SolidHistoryOperation::Sweep(v) | SolidHistoryOperation::Extrusion(v) => {
                version!(v);
                v.reference_point = acadrust::Vector3::ZERO;
                v.sweep_entity = Some(line.clone());
                v.path_entity = Some(line.clone());
            }
            SolidHistoryOperation::Loft(v) => {
                version!(v);
                v.parameters = None;
                v.cross_sections = vec![line.clone(), line.clone()];
                v.guides = vec![line.clone()];
                v.path_entity = Some(line.clone());
            }
            SolidHistoryOperation::Revolve(v) => {
                version!(v);
                v.sweep_entity = Some(line.clone());
            }
            _ => panic!("kind"),
        }
        super::history_author::compare(after, &expected);
        let mut common = old.clone();
        common.data = got.data.clone();
        assert_eq!(&common, got);
    }
    let empty = DwgReader::from_file(format!("{prefix}.empty-brep.dwg"))?.read()?;
    assert_eq!(
        empty.entities().collect::<Vec<_>>(),
        edited.entities().collect::<Vec<_>>()
    );
    assert_eq!(empty.objects.len(), edited.objects.len());
    for (h, old) in &edited.objects {
        let actual = empty.objects.get(h).unwrap();
        let mut expected = old.clone();
        if let ObjectType::DynamicBlock(o) = &mut expected {
            if let DynamicBlockData::SolidHistoryNode(SolidHistoryOperation::Brep(v)) = &mut o.data
            {
                v.acis_data = Default::default();
                v.acis_data.acis_empty_bit = d.version < acadrust::DxfVersion::AC1027;
            }
        }
        assert_eq!(&expected, actual);
    }
    let refilled = DwgReader::from_file(format!("{prefix}.refilled-brep.dwg"))?.read()?;
    assert_eq!(refilled.objects, edited.objects);
    assert_eq!(
        refilled.entities().collect::<Vec<_>>(),
        edited.entities().collect::<Vec<_>>()
    );
    println!("DWG history roots, fifteen node kinds, evaluation graph edges, owners and saved objects match Rust");
    Ok(())
}
