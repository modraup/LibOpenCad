// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{EmbeddedEntity, Line};
use acadrust::objects::*;
use acadrust::{CadDocument, DwgReader, DwgWriter, EntityType, Handle, Vector3};
use std::fs;
fn dynamic(d: &CadDocument, h: Handle) -> &DynamicBlockObject {
    let ObjectType::DynamicBlock(o) = &d.objects[&h] else {
        panic!("dynamic")
    };
    o
}
fn metadata(d: &CadDocument, owner: Handle, key: &str) -> Handle {
    let dict = d
        .extension_dictionary_handle(owner)
        .or_else(|| match &d.objects.get(&owner) {
            Some(ObjectType::DynamicBlock(o)) => o.xdictionary_handle,
            _ => None,
        })
        .unwrap();
    let ObjectType::Dictionary(v) = &d.objects[&dict] else {
        panic!("dictionary")
    };
    v.get(key).unwrap()
}
fn loft(d: &CadDocument, h: Handle) -> &SolidHistoryLoft {
    let DynamicBlockData::SolidHistoryNode(SolidHistoryOperation::Loft(v)) = &dynamic(d, h).data
    else {
        panic!("loft")
    };
    v
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    // Both writers normalize the embedded fit-spline flags on a parameter rewrite.
    DwgWriter::write_to_file(format!("{prefix}.canonical.dwg"), &source)?;
    let canonical = DwgReader::from_file(format!("{prefix}.canonical.dwg"))?.read()?;
    let ids = fs::read_to_string(format!("{prefix}.ids"))?
        .split_whitespace()
        .map(|x| x.parse::<u64>().unwrap())
        .collect::<Vec<_>>();
    let (surface, root, graph, op) = (
        Handle::new(ids[0]),
        Handle::new(ids[1]),
        Handle::new(ids[2]),
        Handle::new(ids[3]),
    );
    let options = metadata(&source, op, "CADCODEC_LOFT_OPTIONS_V1");
    let link = metadata(&source, surface, "CADCODEC_SURFACE_HISTORY_V1");
    let redirected = DwgReader::from_file(format!("{prefix}.raw-reference.dwg"))?.read()?;
    assert_eq!(
        redirected.entities().collect::<Vec<_>>(),
        source.entities().collect::<Vec<_>>()
    );
    assert_eq!(redirected.solid_history_graph(surface).unwrap().root, root);
    for step in [
        "options",
        "cleared",
        "inputs",
        "references",
        "detached",
        "restored",
    ] {
        let got = DwgReader::from_file(format!("{prefix}.{step}.dwg"))?.read()?;
        assert_eq!(got.objects.len(), source.objects.len());
        for e in source.entities() {
            let mut expected = e.clone();
            if step == "detached" && e.common().handle == surface {
                let EntityType::Surface(v) = &mut expected else {
                    panic!("surface")
                };
                v.history_handle = None;
            }
            assert_eq!(
                got.get_entity(e.common().handle),
                Some(&expected),
                "{step}: entity"
            );
        }
        let mut allowed = vec![];
        if step == "options" || step == "cleared" || step == "inputs" {
            allowed.extend([op, options]);
            let mut expected = loft(&canonical, op).clone();
            let v = expected.parameters.as_mut().unwrap();
            v.normals = 4;
            v.start_continuity = 0;
            v.end_continuity = 1;
            v.start_draft_angle = if step == "inputs" { 0.4 } else { 0.2 };
            v.end_draft_angle = 0.6;
            v.start_magnitude = 2.;
            v.end_magnitude = 3.;
            v.start_bulge = 0.7;
            v.end_bulge = 0.9;
            v.closed = true;
            v.periodic = false;
            v.surface = true;
            v.align_direction = false;
            v.section_counts = vec![2, 3];
            let path = EmbeddedEntity::Line(Line {
                start: Vector3::new(7., 8., 9.),
                end: Vector3::new(10., 11., 12.),
                ..Line::new()
            });
            v.path_entity = if step == "options" {
                Some(path.clone())
            } else {
                None
            };
            expected.path_entity = v.path_entity.clone();
            expected.options.start_draft_angle = v.start_draft_angle;
            expected.options.end_draft_angle = v.end_draft_angle;
            expected.options.start_magnitude = v.start_magnitude;
            expected.options.end_magnitude = v.end_magnitude;
            assert_eq!(
                loft(&got, op),
                &expected,
                "{step}: loft native/options/path"
            );
            let ObjectType::XRecord(a) = &source.objects[&options] else {
                panic!("xrecord")
            };
            let ObjectType::XRecord(b) = &got.objects[&options] else {
                panic!("xrecord")
            };
            assert_eq!(
                (a.owner, &a.reactors, &a.object_references),
                (b.owner, &b.reactors, &b.object_references)
            );
        } else {
            allowed.extend([root, graph, op]);
            if step != "detached" {
                assert_eq!(
                    got.solid_history_operation(surface)
                        .unwrap()
                        .base()
                        .unwrap()
                        .node_id(),
                    ids[4] as i32,
                    "Active operation uses history ID, not graph-node ID"
                );
            }
            let mut r = dynamic(&source, root).clone();
            let DynamicBlockData::SolidHistory(v) = &mut r.data else {
                panic!("root")
            };
            v.history_node_id = ids[4] as i32;
            assert_eq!(dynamic(&got, root), &r);
            let mut g = dynamic(&source, graph).clone();
            let DynamicBlockData::EvaluationGraph(v) = &mut g.data else {
                panic!("graph")
            };
            v.nodes[0].expression = Handle::new(ids[5]);
            assert_eq!(dynamic(&got, graph), &g);
            let mut o = dynamic(&source, op).clone();
            let DynamicBlockData::SolidHistoryNode(v) = &mut o.data else {
                panic!("node")
            };
            v.base_mut().unwrap().material = Handle::new(ids[6]);
            assert_eq!(dynamic(&got, op), &o);
            if step == "detached" || step == "restored" {
                allowed.push(link);
                let ObjectType::XRecord(r) = &got.objects[&link] else {
                    panic!("record")
                };
                let value = if step == "detached" {
                    Handle::NULL
                } else {
                    root
                };
                assert_eq!(
                    r.entries.iter().find(|e| e.code == 330).unwrap().value,
                    XRecordValue::Handle(value)
                );
                if !value.is_null() {
                    assert_eq!(r.object_references[0].handle, value);
                }
                assert_eq!(
                    got.solid_history_graph(surface).is_some(),
                    step != "detached"
                );
            }
        }
        for (h, o) in &source.objects {
            if !allowed.contains(h) {
                assert_eq!(
                    got.objects.get(h),
                    Some(o),
                    "{step}: unchanged object {h:?}"
                );
            }
        }
    }
    println!("DWG loft metadata, surface history, active node, graph expression and material references match Rust");
    Ok(())
}
