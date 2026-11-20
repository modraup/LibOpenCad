// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (civil create APIs).
use acadrust::classes::civil::create::cogo_points;
use acadrust::classes::civil::{AlignmentElementKind, AlignmentPiece, ProfilePiece};
use acadrust::entities::{EntityType, ExtendedEntityData};
use acadrust::objects::ObjectType;
use acadrust::DwgReader;

fn arch_group<'a>(doc: &'a acadrust::CadDocument, pred: impl Fn(&acadrust::classes::arch::ArchObject) -> bool) -> &'a acadrust::classes::arch::ArchObject {
    doc.objects
        .values()
        .find_map(|object| match object {
            ObjectType::Arch(a) if a.dxf_name == "AECC_POINT_PG" && pred(a) => Some(a),
            _ => None,
        })
        .expect("point group")
}

fn named_group<'a>(doc: &'a acadrust::CadDocument, name: &str) -> &'a acadrust::classes::arch::ArchObject {
    arch_group(doc, |a| a.decode().strings.first().map(String::as_str) == Some(name))
}

fn all_points<'a>(doc: &'a acadrust::CadDocument) -> &'a acadrust::classes::arch::ArchObject {
    arch_group(doc, |a| a.decode().strings.iter().any(|s| s == "AeccImpAllPointsPG"))
}

fn handle_of(doc: &acadrust::CadDocument, number: u32) -> acadrust::types::Handle {
    doc.entities()
        .find_map(|entity| match entity {
            EntityType::Extended(x) => match &x.data {
                ExtendedEntityData::Arch(d) if d.cogo_point_number() == Some(number) => Some(x.common.handle),
                _ => None,
            },
            _ => None,
        })
        .expect("COGO entity")
}

fn read_dwg(path: &str) -> acadrust::CadDocument {
    DwgReader::from_file(path).unwrap().read().unwrap()
}

fn summary(doc: &acadrust::CadDocument) -> Vec<(u32, (f64, f64, f64), Option<String>)> {
    cogo_points(doc)
        .into_iter()
        .map(|p| (p.number, (p.position.x, p.position.y, p.position.z), p.description))
        .collect()
}

fn alignments(doc: &acadrust::CadDocument) -> Vec<(String, f64, Vec<Vec<(f64, f64)>>)> {
    doc.entities()
        .filter_map(|entity| match entity {
            EntityType::Extended(x) => match &x.data {
                ExtendedEntityData::Arch(a) if a.dxf_name == "AECC_ALIGNMENT" => Some((
                    a.alignment_name().unwrap_or_default(),
                    a.alignment_start_station().unwrap_or(f64::NAN),
                    a.alignment_polylines()
                        .into_iter()
                        .map(|run| run.into_iter().map(|p| (p.x, p.y)).collect())
                        .collect(),
                )),
                _ => None,
            },
            _ => None,
        })
        .collect()
}

fn count_dxf(doc: &acadrust::CadDocument, dxf: &str) -> usize {
    doc.objects
        .values()
        .filter(|object| match object {
            ObjectType::Arch(a) => a.dxf_name == dxf,
            _ => false,
        })
        .count()
}

fn companion_count(doc: &acadrust::CadDocument) -> usize {
    doc.objects
        .values()
        .filter(|object| match object {
            ObjectType::Arch(a) if a.dxf_name == "AECC_FEATURE_STYLE" => {
                a.decode().strings.first().map(|s| s.len() == 32).unwrap_or(false)
            }
            _ => false,
        })
        .count()
}

fn main() {
    let prefix = std::env::args().nth(1).expect("prefix");
    let created = read_dwg(&format!("{prefix}-created.dwg"));
    assert_eq!(
        summary(&created),
        vec![
            (1, (100.0, 200.0, 10.0), Some("TEST".into())),
            (2, (150.0, 250.0, 12.0), None),
            (5, (200.0, 210.0, 14.0), Some("TEST".into())),
        ]
    );
    assert!(cogo_points(&created).iter().all(|p| p.style != 0 && p.label_style != 0));
    assert_eq!(all_points(&created).point_group_numbers(), vec![1, 2, 5]);
    assert_eq!(handle_of(&created, 1).value() != 0, true);

    let updated = read_dwg(&format!("{prefix}-updated.dwg"));
    assert_eq!(
        summary(&updated),
        vec![
            (7, (1.0, 2.0, 3.0), Some("X".into())),
            (5, (200.0, 210.0, 14.0), Some("TEST".into())),
        ]
    );
    assert_eq!(all_points(&updated).point_group_numbers(), vec![5, 7]);

    let groups = read_dwg(&format!("{prefix}-groups.dwg"));
    assert_eq!(named_group(&groups, "G").point_group_numbers(), vec![2, 4, 6]);
    let names: Vec<_> = groups
        .objects
        .values()
        .filter_map(|object| match object {
            ObjectType::Arch(a) if a.dxf_name == "AECC_POINT_GROUP_NODE" => Some(a.decode().strings.clone()),
            _ => None,
        })
        .next()
        .expect("point group node");
    assert_eq!(names, vec!["".to_string(), "G".into()]);

    let renamed = read_dwg(&format!("{prefix}-groups-updated.dwg"));
    assert_eq!(named_group(&renamed, "H").point_group_numbers(), vec![1, 8]);
    assert_eq!(all_points(&renamed).point_group_numbers(), vec![1, 2, 3, 5, 6, 8]);

    let queries = read_dwg(&format!("{prefix}-queries.dwg"));
    let want = [
        ("GA", vec![1u32, 2]),
        ("GB", vec![1, 2, 3, 5, 6]),
        ("GJ", vec![1, 2]),
        ("GK", vec![1, 2, 4]),
        ("GE", vec![4, 5]),
        ("GF", vec![1, 2]),
        ("GG", vec![4, 5]),
        ("GH", vec![1, 2]),
        ("GI", vec![3]),
    ];
    for (name, members) in want {
        assert_eq!(named_group(&queries, name).point_group_numbers(), members, "{name}");
    }

    let updated_q = read_dwg(&format!("{prefix}-queries-updated.dwg"));
    assert_eq!(named_group(&updated_q, "GE").point_group_numbers(), vec![5, 7]);
    assert_eq!(named_group(&updated_q, "GK").point_group_numbers(), vec![1, 2, 4, 7]);
    assert_eq!(named_group(&updated_q, "GA").point_group_numbers(), vec![1, 4]);

    let alignments_doc = read_dwg(&format!("{prefix}-alignments.dwg"));
    let mut listed = alignments(&alignments_doc);
    listed.sort_by(|a, b| a.0.cmp(&b.0));
    assert_eq!(
        listed,
        vec![
            (
                "AL1".into(),
                0.0,
                vec![vec![(0.0, 0.0), (100.0, 0.0), (200.0, 50.0)]]
            ),
            ("AL2".into(), 0.0, vec![vec![(100.0, 0.0), (200.0, 50.0)]]),
        ]
    );
    assert_eq!(companion_count(&alignments_doc), 2);
    assert!(count_dxf(&alignments_doc, "AECC_ALIGNMENT_STYLE") >= 2);
    assert_eq!(count_dxf(&alignments_doc, "AECC_ALIGNMENT_PARCEL_NODE"), 1);
    assert!(count_dxf(&alignments_doc, "AECC_POINT_STYLE") >= 1);

    let fresh = read_dwg(&format!("{prefix}-alignments-new.dwg"));
    assert_eq!(
        alignments(&fresh),
        vec![("AL1".into(), 0.0, vec![vec![(0.0, 0.0), (100.0, 0.0)]])]
    );
    assert_eq!(companion_count(&fresh), 1);
    assert!(count_dxf(&fresh, "AECC_ALIGNMENT_STYLE") >= 1);
    assert_eq!(count_dxf(&fresh, "AECC_ALIGNMENT_PARCEL_NODE"), 1);

    let curves_doc = read_dwg(&format!("{prefix}-alignments-curves.dwg"));
    let named = |doc: &acadrust::CadDocument, name: &str| {
        doc.entities()
            .find_map(|entity| match entity {
                EntityType::Extended(x) => match &x.data {
                    ExtendedEntityData::Arch(a) if a.alignment_name().as_deref() == Some(name) => Some(a.clone()),
                    _ => None,
                },
                _ => None,
            })
            .expect("named alignment")
    };
    let a = named(&curves_doc, "AL1");
    let elements = a.alignment_elements().unwrap();
    let links: Vec<(i16, i16, i16)> = elements.iter().map(|e| (e.id, e.previous, e.next)).collect();
    assert_eq!(links, vec![(1, 0, 5), (2, 5, 3), (3, 2, 6), (4, 6, 0), (5, 1, 2), (6, 3, 4)]);
    assert_eq!(elements[4].kind, AlignmentElementKind::FreeArc { radius: 50.0 });
    assert_eq!(
        elements[0].kind,
        AlignmentElementKind::FixedLine {
            start: acadrust::types::Vector2::new(0.0, 0.0),
            end: acadrust::types::Vector2::new(100.0, 0.0),
        }
    );
    let pieces = a.alignment_pieces();
    let kinds: Vec<AlignmentPiece> = pieces.iter().map(|p| p.0).collect();
    assert_eq!(
        kinds,
        vec![
            AlignmentPiece::Line,
            AlignmentPiece::Curve,
            AlignmentPiece::Line,
            AlignmentPiece::Line,
            AlignmentPiece::Curve,
            AlignmentPiece::Line
        ]
    );
    let t = 50.0 * (0.5f64.atan() / 2.0).tan();
    assert!((pieces[1].1[0].x - (100.0 - t)).abs() < 1e-9);
    let a = named(&curves_doc, "AL2");
    let elements = a.alignment_elements().unwrap();
    assert_eq!(
        elements[2].kind,
        AlignmentElementKind::FreeSpiralCurveSpiral {
            spiral_in: 20.0,
            radius: 50.0,
            spiral_out: 20.0
        }
    );
    let kinds: Vec<AlignmentPiece> = a.alignment_pieces().iter().map(|p| p.0).collect();
    assert_eq!(
        kinds,
        vec![
            AlignmentPiece::Line,
            AlignmentPiece::Spiral,
            AlignmentPiece::Curve,
            AlignmentPiece::Spiral,
            AlignmentPiece::Line
        ]
    );

    let profiles_doc = read_dwg(&format!("{prefix}-profiles.dwg"));
    let named_profile = |doc: &acadrust::CadDocument, name: &str| {
        doc.entities()
            .find_map(|entity| match entity {
                EntityType::Extended(x) => match &x.data {
                    ExtendedEntityData::Arch(a) if a.profile_name().as_deref() == Some(name) => Some(a.clone()),
                    _ => None,
                },
                _ => None,
            })
            .expect("named profile")
    };
    let p1 = named_profile(&profiles_doc, "P1");
    let p2 = named_profile(&profiles_doc, "P2");
    let kinds = |a: &acadrust::classes::arch::ArchEntityData| a.profile_pieces().iter().map(|p| p.0).collect::<Vec<_>>();
    assert_eq!(kinds(&p1), vec![ProfilePiece::Tangent, ProfilePiece::Tangent]);
    assert_eq!(
        kinds(&p2),
        vec![ProfilePiece::Tangent, ProfilePiece::Parabola, ProfilePiece::Tangent]
    );
    assert_eq!(p2.profile_name(), Some("P2".into()));
    let first = p2.profile_pieces()[0].1.last().copied().unwrap();
    assert!((first.x - 40.0).abs() < 1e-9 && (first.y - 18.0).abs() < 1e-9);
    let a = named(&profiles_doc, "AL1");
    let listed: Vec<_> = a.typed_handles()[6..].iter().map(|r| r.0).collect();
    assert_eq!(listed.len(), 2);
    assert_eq!(a.alignment_name(), Some("AL1".into()));

    let fresh_pr = read_dwg(&format!("{prefix}-profiles-new.dwg"));
    let p = named_profile(&fresh_pr, "P1");
    let style_h = p.typed_handles()[11].0;
    let style = match fresh_pr.objects.get(&acadrust::types::Handle::new(style_h)) {
        Some(ObjectType::Arch(a)) => a,
        _ => panic!("profile style"),
    };
    assert_eq!(style.dxf_name, "AECC_PROFILE_STYLE");
    let extension = style.decode().handles[7];
    match fresh_pr.objects.get(&acadrust::types::Handle::new(extension)) {
        Some(ObjectType::Arch(a)) => assert_eq!(a.dxf_name, "AECC_VALIGNMENT_STYLE_EXTENSION"),
        _ => panic!("profile style extension"),
    }

    let pcurves = read_dwg(&format!("{prefix}-profiles-curves.dwg"));
    let p = named_profile(&pcurves, "P1");
    let pieces = p.profile_pieces();
    let kinds: Vec<_> = pieces.iter().map(|p| p.0).collect();
    use ProfilePiece::*;
    assert_eq!(
        kinds,
        vec![Tangent, Circular, Tangent, AsymmetricParabola, Tangent, Parabola, Tangent]
    );
    assert!((pieces[0].1.last().unwrap().x - 42.663_583_344).abs() < 1e-6);
    assert!((pieces[3].1[0].x - 92.0).abs() < 1e-9 && (pieces[3].1.last().unwrap().x - 112.0).abs() < 1e-9);

    let tin_doc = read_dwg(&format!("{prefix}-tin.dwg"));
    let tins = tin_doc
        .entities()
        .filter_map(|entity| match entity {
            EntityType::Extended(x) => match &x.data {
                ExtendedEntityData::Arch(a) => a.tin_surface().map(|tin| {
                    let drawn: Vec<_> = tin.visible_triangles().collect();
                    (tin.vertices.len(), drawn.len(), drawn)
                }),
                _ => None,
            },
            _ => None,
        })
        .collect::<Vec<_>>();
    assert_eq!(tins.len(), 1);
    assert_eq!(tins[0].0, 7);
    assert_eq!(tins[0].1, 1);
    let pts = [
        acadrust::types::Vector3::new(0.0, 0.0, 0.0),
        acadrust::types::Vector3::new(10.0, 0.0, 1.0),
        acadrust::types::Vector3::new(0.0, 10.0, 2.0),
    ];
    assert!(pts.iter().all(|p| tins[0].2[0].contains(p)));
    let style = tin_doc
        .objects
        .values()
        .find_map(|object| match object {
            ObjectType::Arch(a) if a.dxf_name == "AECC_SURFACE_STYLE" => Some(a.decode()),
            _ => None,
        })
        .expect("surface style");
    assert!(style.strings.len() == 1 + 17 + 2 * 25);
    assert!(style.strings.iter().any(|s| s.ends_with(";ANSI31")));

    let dense = read_dwg(&format!("{prefix}-tin-dense.dwg"));
    let dense_tin = dense
        .entities()
        .find_map(|entity| match entity {
            EntityType::Extended(x) => match &x.data {
                ExtendedEntityData::Arch(a) => a.tin_surface(),
                _ => None,
            },
            _ => None,
        })
        .expect("dense TIN");
    assert_eq!(dense_tin.vertices.len(), 14);
    assert_eq!(dense_tin.visible_triangles().count(), 11);
    println!("Civil COGO, point-group, siteless-alignment and TIN drawings match local Rust");
}
