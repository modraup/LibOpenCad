// SPDX-License-Identifier: MPL-2.0
use acadrust::classes::DxfClass;
use acadrust::objects::*;
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType, Handle, Line, Vector3};
use std::path::Path;
fn register(d: &mut CadDocument, name: &str, cpp: &str) {
    d.classes.push_preserving(DxfClass::new(name, cpp));
}
fn insert(
    d: &mut CadDocument,
    owner: Handle,
    name: &str,
    cpp: &str,
    data: AssociativeData,
) -> Handle {
    let handle = d.allocate_handle();
    d.objects.insert(
        handle,
        ObjectType::Associative(AssociativeObject {
            handle,
            owner,
            dxf_name: name.into(),
            cpp_class_name: cpp.into(),
            data,
            ..Default::default()
        }),
    );
    handle
}
fn action(owning: Handle, body: Handle) -> AssocAction {
    AssocAction {
        class_version: 2,
        owning_network: owning,
        action_body: body,
        action_index: 1,
        max_dependency_index: 4,
        ..Default::default()
    }
}
pub fn build(version: DxfVersion) -> CadDocument {
    let mut d = CadDocument::with_version(version);
    let line = d
        .add_entity(EntityType::Line(Line::from_coords(0., 0., 0., 10., 0., 0.)))
        .unwrap();
    let nod = d.header.named_objects_dict_handle;
    let net_a = d.allocate_handle();
    let net_b = d.allocate_handle();
    let act = d.allocate_handle();
    let var = d.allocate_handle();
    let grp = d.allocate_handle();
    let dep = insert(
        &mut d,
        nod,
        "ACDBASSOCDEPENDENCY",
        "AcDbAssocDependency",
        AssociativeData::Dependency(AssocDependency {
            class_version: 2,
            is_read_dependency: true,
            is_attached_to_object: true,
            dependent_on: line,
            name: Some("依赖".into()),
            ..Default::default()
        }),
    );
    let value_dep = insert(
        &mut d,
        nod,
        "ACDBASSOCVALUEDEPENDENCY",
        "AcDbAssocValueDependency",
        AssociativeData::ValueDependency(AssocValueDependency {
            dependency: AssocDependency {
                class_version: 2,
                dependent_on: var,
                ..Default::default()
            },
            class_version: 2,
            name: "k1".into(),
            value: AssocEvalVariant {
                code: 40,
                value: AssocEvalValue::Real(13.0),
            },
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCGEOMDEPENDENCY",
        "AcDbAssocGeomDependency",
        AssociativeData::GeomDependency(AssocGeomDependency {
            dependency: AssocDependency {
                class_version: 2,
                dependent_on: line,
                ..Default::default()
            },
            class_version: 2,
            enabled: true,
            persistent_subent: AssocPersistentSubentId {
                class_name: "AcDbAssocSingleEdgePersSubentId".into(),
                class_code: 1,
                dependent_on_compound_object: true,
                ..Default::default()
            },
        }),
    );
    d.objects.insert(
        act,
        ObjectType::Associative(AssociativeObject {
            handle: act,
            owner: net_a,
            dxf_name: "ACDBASSOCACTION".into(),
            cpp_class_name: "AcDbAssocAction".into(),
            data: AssociativeData::Action(AssocAction {
                class_version: 2,
                owning_network: net_a,
                action_body: dep,
                owned_parameters: vec![value_dep],
                values: vec![AssocValueParam {
                    class_version: 1,
                    name: "Distance".into(),
                    unit_type: 1,
                    variables: vec![AssocValueParamVariable {
                        value: AssocEvalVariant {
                            code: 40,
                            value: AssocEvalValue::Real(10.0),
                        },
                        handle: var,
                    }],
                    controlled_object_dependency: dep,
                }],
                ..Default::default()
            }),
            ..Default::default()
        }),
    );
    d.objects.insert(
        var,
        ObjectType::Associative(AssociativeObject {
            handle: var,
            owner: net_a,
            dxf_name: "ACDBASSOCVARIABLE".into(),
            cpp_class_name: "AcDbAssocVariable".into(),
            data: AssociativeData::Variable(AssocVariable {
                action: action(net_a, Handle::NULL),
                class_version: 2,
                name: "长度".into(),
                expression: "13".into(),
                evaluator: "AcDbAssocVariable".into(),
                description: "linear".into(),
                value: AssocEvalVariant {
                    code: 40,
                    value: AssocEvalValue::Real(13.0),
                },
                dependencies: vec![AssocVariableDependency {
                    dependency: value_dep,
                    flags: 0,
                }],
                ..Default::default()
            }),
            ..Default::default()
        }),
    );
    let mut group = Assoc2dConstraintGroup {
        action: action(net_a, Handle::NULL),
        version: 2,
        work_plane: [
            Vector3::new(0., 0., 0.),
            Vector3::new(1., 0., 0.),
            Vector3::new(0., 1., 0.),
        ],
        dependency: dep,
        next_node_id: 6,
        ..Default::default()
    };
    group.nodes.push(AssocConstraintNode {
        node_id: 1,
        status: 1,
        connections: vec![2, 3],
        ..Default::default()
    });
    group.nodes.push(AssocConstraintNode {
        node_id: 2,
        class_name: "AcConstrainedPoint".into(),
        data: AssocConstraintNodeData::Point {
            geometry_dependency: dep,
            geometry_node_id: 2,
            point: Some(Vector3::new(1., 2., 0.)),
        },
        ..Default::default()
    });
    group.nodes.push(AssocConstraintNode {
        node_id: 3,
        class_name: "AcConstrainedLine".into(),
        data: AssocConstraintNodeData::Line {
            geometry_dependency: dep,
            geometry_node_id: 3,
            point: Vector3::new(0., 0., 0.),
            direction: Vector3::new(1., 0., 0.),
        },
        ..Default::default()
    });
    group.nodes.push(AssocConstraintNode {
        node_id: 4,
        class_name: "AcDistanceConstraint".into(),
        data: AssocConstraintNodeData::Distance {
            owner_id: 1,
            is_implied: false,
            is_active: true,
            value_dependency: value_dep,
            dimension_dependency: dep,
            direction_type: 0,
            distance: None,
        },
        ..Default::default()
    });
    group.nodes.push(AssocConstraintNode {
        node_id: 5,
        class_name: "AcHorizontalConstraint".into(),
        data: AssocConstraintNodeData::Parallel {
            owner_id: 1,
            is_implied: false,
            is_active: true,
            datum_line_index: Some(3),
        },
        ..Default::default()
    });
    d.objects.insert(
        grp,
        ObjectType::Associative(AssociativeObject {
            handle: grp,
            owner: net_a,
            dxf_name: "ACDBASSOC2DCONSTRAINTGROUP".into(),
            cpp_class_name: "AcDbAssoc2dConstraintGroup".into(),
            data: AssociativeData::ConstraintGroup(group),
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCPLANESURFACEACTIONBODY",
        "AcDbAssocPlaneSurfaceActionBody",
        AssociativeData::SurfaceActionBody(AssocSurfaceActionBody {
            kind: AssocSurfaceActionKind::Plane,
            action_body: AssocActionBody { version: 2 },
            surface_body: AssocSurfaceBody {
                version: 2,
                dependency: dep,
                ..Default::default()
            },
            class_version: 2,
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCARRAYACTIONBODY",
        "AcDbAssocArrayActionBody",
        AssociativeData::ArrayActionBody(AssocArrayActionBody {
            action_body: AssocActionBody { version: 2 },
            version: 3,
            parameter_block: "AcDbAssocArrayRectangularParameters".into(),
            item_list_version: 1,
            item_class: "AcDbAssocArrayItem".into(),
            items: vec![AssocArrayItem {
                class_version: 1,
                location: [1, 0, 0],
                uses_default_transform: true,
                x_direction: Vector3::new(1., 0., 0.),
                first_handle: Some(line),
                ..Default::default()
            }],
            transform: [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.],
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBDIMASSOC",
        "AcDbDimAssoc",
        AssociativeData::DimensionAssociation(AssocDimensionAssociation {
            associativity: 1,
            dimension: line,
            references: [
                vec![AssocDimensionReference {
                    class_name: "AcDbOsnapPointRef".into(),
                    osnap_type: 1,
                    xrefs: vec![line],
                    osnap_point: Vector3::new(10., 0., 0.),
                    ..Default::default()
                }],
                Vec::new(),
                Vec::new(),
                Vec::new(),
            ],
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCCOMPOUNDACTIONPARAM",
        "AcDbAssocCompoundActionParam",
        AssociativeData::CompoundActionParam(AssocCompoundActionParam {
            action_param: AssocActionParam {
                is_r2013: 1,
                version: 1,
                name: "compound".into(),
            },
            class_version: 1,
            parameters: vec![act],
            child_parameter: Some(AssocChildParameter {
                status: 1,
                id: 2,
                parameter: var,
                secondary_parameter: dep,
                marker: 1,
                tertiary_parameter: value_dep,
            }),
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCVIEWREPACTIONBODY",
        "AcDbAssocViewRepActionBody",
        AssociativeData::ViewRepActionBody(AssocViewRepActionBody {
            action_body: AssocActionBody { version: 2 },
            class_version: 1,
            view_rep: line,
            view_type: 1,
            rotation: 0.25,
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBCENTERMARKACTIONBODY",
        "AcDbCenterMarkActionBody",
        AssociativeData::SmartCenterActionBody(AssocSmartCenterActionBody {
            action_body: AssocActionBody { version: 2 },
            version: 1,
            ..Default::default()
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCEDGEACTIONPARAM",
        "AcDbAssocEdgeActionParam",
        AssociativeData::EdgeActionParam(AssocEdgeActionParam {
            single_dependency: AssocSingleDependencyActionParam {
                action_param: AssocActionParam {
                    is_r2013: 1,
                    name: "edge".into(),
                    ..Default::default()
                },
                dependency_class_version: 1,
                dependency: dep,
                class_version: 1,
            },
            parameter: act,
            has_action: true,
            action_type: 19,
            subcurve_kind: AssocSubcurveKind::Line,
            curve: vec![
                AssocCurveValue::Bool(true),
                AssocCurveValue::Int(2),
                AssocCurveValue::Real(3.5),
                AssocCurveValue::Point(Vector3::new(1., 2., 3.)),
            ],
        }),
    );
    insert(
        &mut d,
        nod,
        "ACDBASSOCRESTOREENTITYSTATEACTIONBODY",
        "AcDbAssocRestoreEntityStateActionBody",
        AssociativeData::AnnotationActionBody(AssocAnnotationActionBody {
            kind: AssocAnnotationKind::RestoreEntityState,
            action_body: AssocActionBody { version: 2 },
            class_version: 1,
            entity: line,
            ..Default::default()
        }),
    );
    let shared = AssocActionDependency {
        is_owned: false,
        dependency: act,
    };
    d.objects.insert(
        net_a,
        ObjectType::Associative(AssociativeObject {
            handle: net_a,
            owner: nod,
            dxf_name: "ACDBASSOCNETWORK".into(),
            cpp_class_name: "AcDbAssocNetwork".into(),
            data: AssociativeData::Network(AssocNetwork {
                action: action(Handle::NULL, Handle::NULL),
                network_version: 2,
                network_action_index: 3,
                actions: vec![shared.clone()],
                owned_actions: vec![act, var, grp, net_b],
            }),
            ..Default::default()
        }),
    );
    d.objects.insert(
        net_b,
        ObjectType::Associative(AssociativeObject {
            handle: net_b,
            owner: net_a,
            dxf_name: "ACDBASSOCNETWORK".into(),
            cpp_class_name: "AcDbAssocNetwork".into(),
            data: AssociativeData::Network(AssocNetwork {
                action: action(net_a, Handle::NULL),
                network_version: 2,
                network_action_index: 1,
                actions: vec![shared],
                owned_actions: vec![net_a],
            }),
            ..Default::default()
        }),
    );
    if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&nod) {
        dict.add_entry("ACAD_ASSOCNETWORK", net_a);
    }
    for (name, cpp) in [
        ("ACDBASSOCDEPENDENCY", "AcDbAssocDependency"),
        ("ACDBASSOCVALUEDEPENDENCY", "AcDbAssocValueDependency"),
        ("ACDBASSOCGEOMDEPENDENCY", "AcDbAssocGeomDependency"),
        ("ACDBASSOCACTION", "AcDbAssocAction"),
        ("ACDBASSOCVARIABLE", "AcDbAssocVariable"),
        ("ACDBASSOC2DCONSTRAINTGROUP", "AcDbAssoc2dConstraintGroup"),
        ("ACDBASSOCPLANESURFACEACTIONBODY", "AcDbAssocPlaneSurfaceActionBody"),
        ("ACDBASSOCARRAYACTIONBODY", "AcDbAssocArrayActionBody"),
        ("ACDBDIMASSOC", "AcDbDimAssoc"),
        ("ACDBASSOCCOMPOUNDACTIONPARAM", "AcDbAssocCompoundActionParam"),
        ("ACDBASSOCVIEWREPACTIONBODY", "AcDbAssocViewRepActionBody"),
        ("ACDBCENTERMARKACTIONBODY", "AcDbCenterMarkActionBody"),
        ("ACDBASSOCEDGEACTIONPARAM", "AcDbAssocEdgeActionParam"),
        (
            "ACDBASSOCRESTOREENTITYSTATEACTIONBODY",
            "AcDbAssocRestoreEntityStateActionBody",
        ),
        ("ACDBASSOCNETWORK", "AcDbAssocNetwork"),
    ] {
        register(&mut d, name, cpp);
    }
    d
}
pub fn objects(d: &CadDocument) -> Vec<AssociativeObject> {
    let mut values: Vec<_> = d
        .objects
        .values()
        .filter_map(|object| match object {
            ObjectType::Associative(value) => Some(value.clone()),
            _ => None,
        })
        .collect();
    values.sort_by_key(|object| object.handle.value());
    for object in &mut values {
        object.source_version = None;
    }
    values
}
pub fn generate(out: &str) -> acadrust::Result<()> {
    std::fs::create_dir_all(out)?;
    for version in [DxfVersion::AC1024, DxfVersion::AC1032] {
        let d = build(version);
        let name = format!("{version:?}");
        DxfWriter::new(&d).write_to_file(Path::new(out).join(format!("{name}-ascii.dxf")))?;
        DxfWriter::new_binary(&d).write_to_file(Path::new(out).join(format!("{name}-binary.dxf")))?;
    }
    Ok(())
}
pub fn verify(source: &str, out: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(source)?.read()?;
    let expected = objects(&original);
    assert!(
        expected.len() >= 12,
        "{}: expected typed associative objects",
        source
    );
    for suffix in ["-ascii.dxf", "-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        assert_eq!(
            objects(&restored),
            expected,
            "{source} vs {out}{suffix}: associative objects"
        );
    }
    Ok(())
}
