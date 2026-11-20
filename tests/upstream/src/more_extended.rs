// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::extended_entity::*;
use acadrust::entities::{EntityCommon, ExtendedEntity, ExtendedEntityData as E};
use acadrust::types::Color;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
pub(crate) fn items() -> Vec<E> {
    let mut values = vec![
        E::Camera {
            view_handle: Handle::NULL,
        },
        E::SectionObject(SectionObjectData {
            state: 2,
            flags: 3,
            name: "剖切".into(),
            vertical_direction: Vector3::UNIT_Z,
            top_height: 8.,
            bottom_height: -2.,
            indicator_alpha: 50,
            indicator_color: Color::from_index(3),
            vertices: vec![Vector3::ZERO, Vector3::new(1., 2., 3.)],
            back_line_vertices: vec![Vector3::new(4., 5., 6.)],
            settings_handle: Handle::NULL,
        }),
        E::ArcAlignedText(ArcAlignedTextData {
            text: "圆弧文字".into(),
            font_name: "txt.shx".into(),
            big_font_name: "".into(),
            style_name: "Standard".into(),
            center: Vector3::new(1., 2., 3.),
            radius: 8.,
            x_scale: 1.5,
            text_size: 2.,
            character_spacing: 0.2,
            offset_from_arc: 1.,
            right_offset: 2.,
            left_offset: 3.,
            start_angle: 0.5,
            end_angle: 1.5,
            reverse: true,
            text_direction: 1,
            alignment: 2,
            text_position: 1,
            bold: true,
            italic: true,
            underlined: true,
            character_set: 134,
            pitch_and_family: 1,
            is_shx: true,
            text_color: 3,
            normal: Vector3::UNIT_Z,
            wizard_flag: true,
            arc_handle: Handle::NULL,
        }),
        E::RemoteText(RemoteTextData {
            position: Vector3::new(2., 3., 4.),
            normal: Vector3::UNIT_Z,
            rotation: 0.25,
            height: 2.,
            style_handle: Handle::NULL,
            style_name: "Standard".into(),
            flags: 2,
            text: "外部文本".into(),
        }),
        E::GeoPositionMarker(GeoPositionMarkerData {
            class_version: 1,
            position: Vector3::new(1., 3., 5.),
            radius: 2.,
            notes: "位置标记".into(),
            landing_gap: 0.5,
            mtext_visible: true,
            text_alignment: 2,
            enable_frame_text: true,
            embedded_mtext: Some({
                let mut m = acadrust::entities::MText::default();
                m.insertion_point = Vector3::new(4., 5., 6.);
                m.normal = Vector3::UNIT_Z;
                m.height = 2.5;
                m.rectangle_width = 20.;
                m.value = "嵌入位置说明".into();
                m.style = "Standard".into();
                m
            }),
        }),
        E::CoordinationModel(CoordinationModelData {
            flags: 3,
            definition_handle: Handle::NULL,
            transform: [
                1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 1., 2., 3., 1.,
            ],
            unit_factor: 2.,
        }),
        E::OleFrame(OleFrameData {
            flag: 2,
            mode: 3,
            storage: acadrust::compound_file::StructuredStoragePayload::decode(&[1, 2, 3, 4]),
        }),
        E::LayoutPrintConfig(LayoutPrintConfigData {
            flag: 7,
            ..Default::default()
        }),
        E::Format(FormatData::default()),
    ];
    values.extend(cloud_items());
    values
}
fn cloud_items() -> Vec<E> {
    let cloud = PointCloudData {
        class_version: 2,
        origin: Vector3::new(1., 2., 3.),
        saved_filename: "旧点云.pc3".into(),
        source_files: vec![],
        extents_min: Vector3::new(-1., -2., -3.),
        extents_max: Vector3::new(4., 5., 6.),
        point_count: 1234,
        ucs_name: "坐标".into(),
        ucs_origin: Vector3::ZERO,
        ucs_x_direction: Vector3::UNIT_X,
        ucs_y_direction: Vector3::UNIT_Y,
        ucs_z_direction: Vector3::UNIT_Z,
        definition_handle: Handle::NULL,
        reactor_handle: Handle::NULL,
        show_intensity: true,
        intensity_scheme: 2,
        minimum_intensity: 1.,
        maximum_intensity: 200.,
        low_intensity_threshold: 10.,
        high_intensity_threshold: 180.,
        show_clipping: false,
        clippings: vec![],
    };
    let mut files = cloud.clone();
    files.source_files = vec!["甲.las".into(), "乙.las".into()];
    let crop = PointCloudExCrop {
        crop_type: 2,
        inside: true,
        inverted: false,
        plane: Vector3::UNIT_Z,
        x_direction: Vector3::UNIT_X,
        y_direction: Vector3::UNIT_Y,
        points: vec![
            Vector3::ZERO,
            Vector3::new(1., 2., 3.),
            Vector3::new(4., 5., 6.),
        ],
    };
    let ex = PointCloudExData {
        class_version: 1,
        extents_min: Vector3::ZERO,
        extents_max: Vector3::new(10., 20., 30.),
        ucs_origin: Vector3::ZERO,
        ucs_x_direction: Vector3::UNIT_X,
        ucs_y_direction: Vector3::UNIT_Y,
        ucs_z_direction: Vector3::UNIT_Z,
        locked: true,
        definition_handle: Handle::NULL,
        reactor_handle: Handle::NULL,
        name: "新点云".into(),
        show_intensity: true,
        show_cropping: true,
        hidden_scans: vec!["扫描甲".into(), "扫描乙".into()],
        hidden_regions: vec![2, 4],
        stylization_type: 2,
        intensity_color_scheme: "强度".into(),
        current_color_scheme: "颜色".into(),
        classification_color_scheme: "分类".into(),
        elevation_min: -2.,
        elevation_max: 40.,
        intensity_min: 10,
        intensity_max: 200,
        intensity_out_of_range_behavior: 1,
        elevation_out_of_range_behavior: 2,
        elevation_apply_to_fixed_range: true,
        intensity_as_gradient: true,
        elevation_as_gradient: false,
        croppings: vec![
            crop.clone(),
            PointCloudExCrop {
                inverted: true,
                ..crop
            },
        ],
    };
    vec![
        E::PointCloud(cloud),
        E::PointCloud(files),
        E::PointCloudEx(ex),
    ]
}

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for data in items() {
        let e = EntityType::Extended(Box::new(ExtendedEntity {
            common: EntityCommon::new(),
            data,
        }));
        d.add_entity(e.clone())?;
        d.add_paper_space_entity(e)?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn data(e: &EntityType) -> &E {
    if let EntityType::Extended(e) = e {
        &e.data
    } else {
        panic!("expected extended {e:?}")
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for e in src.entities() {
        assert_eq!(round.get_entity(e.common().handle), Some(e));
    }
    let edited = read("edited")?;
    for e in src.entities_in_block("*Paper_Space") {
        assert_eq!(edited.get_entity(e.common().handle), Some(e));
    }
    for e in edited.entities_in_block("*Model_Space") {
        match data(e) {
            E::Camera { .. } => {}
            E::SectionObject(v) => assert_eq!(v.vertices[0].z, 9.),
            E::ArcAlignedText(v) => assert_eq!(v.text, "修改圆弧"),
            E::RemoteText(v) => assert_eq!(v.rotation, 0.75),
            E::GeoPositionMarker(v) => assert_eq!(v.landing_gap, 4.),
            E::CoordinationModel(v) => assert_eq!(v.transform[12], 9.),
            E::OleFrame(v) => assert_eq!(v.storage.encode(), vec![9, 8, 7]),
            E::LayoutPrintConfig(v) => assert_eq!(v.flag, 12),
            E::Format(_) => {}
            E::PointCloud(v) => assert_eq!(v.origin.z, 99.),
            E::PointCloudEx(v) => {
                assert_eq!(v.croppings[0].points[1].z, 99.);
                assert!(v.croppings[1].inverted);
                assert_eq!(v.hidden_regions, vec![2, 4]);
            }
            _ => panic!(),
        }
    }
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 36);
    for copy in copied
        .entities()
        .filter(|e| src.get_entity(e.common().handle).is_none())
    {
        assert!(
            edited
                .entities_in_block("*Model_Space")
                .any(|e| match (data(e), data(copy)) {
                    (E::Format(a), E::Format(b)) => {
                        let payload = |v: &FormatData| {
                            v.raw_dxf_codes
                                .clone()
                                .unwrap_or_default()
                                .into_iter()
                                .filter(|(c, _)| !matches!(c, 5 | 330 | 67))
                                .collect::<Vec<_>>()
                        };
                        payload(a) == payload(b)
                    }
                    (a, b) => a == b,
                }),
            "copy {:?}",
            copy
        );
    }
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 24);
    for e in edited.entities() {
        assert_eq!(deleted.get_entity(e.common().handle), Some(e));
    }
    let fresh = read("fresh")?;
    assert_eq!(fresh.entities().count(), 12);
    println!("Rust verified twelve extended entity cases including both point-cloud formats, repeated fields, copy/delete and neighboring space");
    Ok(())
}
