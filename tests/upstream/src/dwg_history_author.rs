// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::*;
use acadrust::{DwgReader, EntityType, Handle};
use std::fs;
fn history(d: &acadrust::CadDocument, h: Handle) -> &SolidHistoryOperation {
    let ObjectType::DynamicBlock(o) = &d.objects[&h] else {
        panic!("history object")
    };
    let DynamicBlockData::SolidHistoryNode(v) = &o.data else {
        panic!("history node")
    };
    v
}
fn kind(op: &SolidHistoryOperation) -> usize {
    super::solid_history::kind(op)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    let manifest = fs::read_to_string(format!("{prefix}.manifest"))?;
    let ids = manifest
        .split_whitespace()
        .map(|v| v.parse::<u64>().unwrap())
        .collect::<Vec<_>>();
    let (solid, surface, fresh, loft_node) = (
        Handle::new(ids[0]),
        Handle::new(ids[1]),
        Handle::new(ids[2]),
        Handle::new(ids[3]),
    );
    let authored = DwgReader::from_file(format!("{prefix}.authored.dwg"))?.read()?;
    let ag = authored.solid_history_graph(solid).unwrap();
    assert_eq!(ag.nodes.len(), 15);
    let kinds = fs::read_to_string(format!("{prefix}.kinds"))?;
    let expected = kinds
        .lines()
        .filter(|l| !l.is_empty())
        .map(|l| l.parse::<usize>().unwrap())
        .collect::<Vec<_>>();
    assert_eq!(expected.len(), 15);
    for (h, k) in ag.nodes.iter().zip(&expected) {
        assert_eq!(kind(history(&authored, *h)), *k);
    }
    let loft = history(&authored, loft_node);
    let SolidHistoryOperation::Loft(v) = loft else {
        panic!("loft")
    };
    assert_eq!(v.parameters.as_ref().map(|p| p.normals), Some(3));
    if let Some(old) = source.solid_history_graph(solid) {
        assert!(!authored.objects.contains_key(&old.root));
        for h in old.nodes {
            assert!(!authored.objects.contains_key(&h));
        }
    }
    match (
        source.get_entity(solid).unwrap(),
        authored.get_entity(solid).unwrap(),
    ) {
        (EntityType::Solid3D(a), EntityType::Solid3D(b)) => assert_eq!(a.acis_data, b.acis_data),
        _ => panic!("solid"),
    }
    let surface_new = DwgReader::from_file(format!("{prefix}.surface.dwg"))?.read()?;
    let sg = surface_new.solid_history_graph(surface).unwrap();
    assert_eq!(sg.nodes.len(), 2);
    assert_eq!(kind(history(&surface_new, sg.nodes[0])), 0);
    assert_eq!(kind(history(&surface_new, sg.nodes[1])), 4);
    if let Some(old) = source.solid_history_graph(surface) {
        assert!(!surface_new.objects.contains_key(&old.root));
    }
    match (
        source.get_entity(surface).unwrap(),
        surface_new.get_entity(surface).unwrap(),
    ) {
        (EntityType::Surface(a), EntityType::Surface(b)) => assert_eq!(a.acis_data, b.acis_data),
        _ => panic!("surface"),
    }
    let fresh_doc = DwgReader::from_file(format!("{prefix}.fresh.dwg"))?.read()?;
    let fg = fresh_doc.solid_history_graph(fresh).unwrap();
    assert_eq!(fg.nodes.len(), 2);
    assert_eq!(kind(history(&fresh_doc, fg.nodes[0])), 0);
    assert_eq!(kind(history(&fresh_doc, fg.nodes[1])), 4);
    let lofted = DwgReader::from_file(format!("{prefix}.loft.dwg"))?.read()?;
    let lg = lofted.solid_history_graph(fresh).unwrap();
    let SolidHistoryOperation::Loft(v) = history(&lofted, lg.nodes[0]) else {
        panic!("fresh loft")
    };
    assert_eq!(v.parameters.as_ref().map(|p| p.normals), Some(5));
    let options = DwgReader::from_file(format!("{prefix}.options.dwg"))?.read()?;
    let og = options.solid_history_graph(fresh).unwrap();
    let SolidHistoryOperation::Loft(v) = history(&options, og.nodes[0]) else {
        panic!("options")
    };
    assert_eq!(v.parameters.as_ref().map(|p| p.normals), Some(2));
    if let Ok(mut region) = DwgReader::from_file(format!("{prefix}.region.dwg")) {
        let region = region.read()?;
        // The pinned DWG reader restores REGION history handles only through
        // leftover handle-stream bytes on BODY; native still stores the slot.
        assert!(region.entities().any(|e| matches!(e, EntityType::Region(_))));
        assert!(region
            .objects
            .values()
            .any(|o| matches!(o, ObjectType::DynamicBlock(o) if matches!(o.data, DynamicBlockData::SolidHistory(_)))));
    }
    for class in [
        "ACSH_HISTORY_CLASS",
        "ACAD_EVALUATION_GRAPH",
        "ACSH_BOX_CLASS",
        "ACSH_LOFT_CLASS",
    ] {
        assert!(
            authored
                .classes
                .get_by_name(class)
                .map(|c| c.instance_count)
                .unwrap_or(0)
                > 0,
            "{class}"
        );
    }
    println!("DWG history authoring matches Rust");
    Ok(())
}
