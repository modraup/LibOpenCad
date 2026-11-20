// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::{
    ClassObjectData, ModelDocViewStyle, ObjectDataField, ObjectDataType, ObjectDataValue, ObjectType,
    ViewRepSketchGeometry,
};
use acadrust::types::{Color, DxfVersion, Handle, Vector2, Vector3};
use acadrust::{DwgReader, DxfReader};

fn class_data<'a>(doc: &'a acadrust::CadDocument, name: &str) -> &'a ClassObjectData {
    doc.objects
        .values()
        .find_map(|object| match object {
            ObjectType::ClassObject(object) if object.dxf_name() == name => Some(&object.data),
            _ => None,
        })
        .unwrap_or_else(|| panic!("{name}"))
}

fn check_class(doc: &acadrust::CadDocument, name: &str) {
    let class = doc.classes.get_by_name(name).unwrap_or_else(|| panic!("{name} class"));
    assert!(!class.is_an_entity, "{name}");
    assert_eq!(class.item_class_id, 499, "{name}");
}

fn dxf(path: &str) -> acadrust::CadDocument {
    DxfReader::from_file(path)
        .unwrap_or_else(|e| panic!("{path}: {e}"))
        .read()
        .unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn dwg(path: &str) -> acadrust::CadDocument {
    DwgReader::from_file(path)
        .unwrap_or_else(|e| panic!("{path}: {e}"))
        .read()
        .unwrap_or_else(|e| panic!("{path}: {e}"))
}

fn check_render(data: &ClassObjectData, predefined: bool) {
    let ClassObjectData::RenderSettings(value) = data else {
        panic!("RENDERSETTINGS");
    };
    assert_eq!(value.class_version, 2);
    assert_eq!(value.name, "High");
    assert!(value.fog_enabled);
    assert!(value.backfaces_enabled);
    assert_eq!(value.display_index, 9);
    assert_eq!(value.has_predefined, predefined);
}

fn check_rapid(data: &ClassObjectData) {
    let ClassObjectData::RapidRtRenderSettings(value) = data else {
        panic!("RAPIDRTRENDERSETTINGS");
    };
    assert_eq!(value.base.class_version, 2);
    assert_eq!(value.base.name, "RapidRT");
    assert_eq!(value.render_level, 5);
    assert!(value.base.has_predefined);
    assert!((value.filter_width - 2.0).abs() < 1e-12);
}

fn check_mental(data: &ClassObjectData) {
    let ClassObjectData::MentalRayRenderSettings(value) = data else {
        panic!("MENTALRAYRENDERSETTINGS");
    };
    assert_eq!(value.base.class_version, 4);
    assert_eq!(value.base.name, "RaySettings");
    assert_eq!(value.tile_size, 64);
    assert!((value.energy_multiplier - 0.875).abs() < 1e-12);
    assert!((value.sampling_contrast[3] - 0.4).abs() < 1e-12);
    assert!(!value.final_gathering_sample_radius_state[1]);
}

fn check_sections(data: &ClassObjectData, handle: u64) {
    let ClassObjectData::SectionManager(value) = data else {
        panic!("SECTION_MANAGER");
    };
    assert!(value.is_live);
    assert_eq!(value.sections, vec![Handle::new(handle)]);
}

fn check_gradient(data: &ClassObjectData) {
    let ClassObjectData::GradientBackground(value) = data else {
        panic!("GRADIENT_BACKGROUND");
    };
    assert_eq!(value.class_version, 1);
    assert_eq!(value.color_top, 0x00ff0000);
    assert!((value.horizon - 0.25).abs() < 1e-12);
    assert!((value.rotation - 1.25).abs() < 1e-12);
}

fn check_ground(data: &ClassObjectData) {
    let ClassObjectData::GroundPlaneBackground(value) = data else {
        panic!("GROUND_PLANE_BACKGROUND");
    };
    assert_eq!(value.color_sky_zenith, 0x00111111);
    assert_eq!(value.color_far, 0x00666666);
}

fn check_ibl(data: &ClassObjectData, handle: u64) {
    let ClassObjectData::IblBackground(value) = data else {
        panic!("RAPIDRTRENDERENVIRONMENT");
    };
    assert!(value.enabled);
    assert_eq!(value.name, "studio.hdr");
    assert!((value.rotation - 0.75).abs() < 1e-12);
    assert_eq!(value.secondary_background, Handle::new(handle));
}

fn check_image(data: &ClassObjectData, full_xy: bool) {
    let ClassObjectData::ImageBackground(value) = data else {
        panic!("IMAGE_BACKGROUND");
    };
    assert_eq!(value.filename, "bg.png");
    assert!(value.fit_to_screen);
    assert!(value.use_tiling);
    assert!((value.offset.x - 1.5).abs() < 1e-12);
    assert!((value.scale.x - 3.5).abs() < 1e-12);
    if full_xy {
        assert!((value.offset.y - 2.5).abs() < 1e-12);
        assert!((value.scale.y - 4.5).abs() < 1e-12);
    }
}

fn check_sky(data: &ClassObjectData, handle: u64) {
    let ClassObjectData::SkyLightBackground(value) = data else {
        panic!("SKYLIGHT_BACKGROUND");
    };
    assert_eq!(value.sun, Handle::new(handle));
}

fn check_solid(data: &ClassObjectData) {
    let ClassObjectData::SolidBackground(value) = data else {
        panic!("SOLID_BACKGROUND");
    };
    assert_eq!(value.color, 0x00112233);
}

fn check_entry(data: &ClassObjectData) {
    let ClassObjectData::RenderEntry(value) = data else {
        panic!("RENDERENTRY");
    };
    assert_eq!(value.image_filename, "shot.png");
    assert_eq!(value.width, 1920);
    assert_eq!(value.end_millisecond, 13);
    assert!((value.render_time - 12.5).abs() < 1e-12);
}

fn check_environment(data: &ClassObjectData) {
    let ClassObjectData::RenderEnvironment(value) = data else {
        panic!("RENDERENVIRONMENT");
    };
    assert!(value.fog_enabled);
    assert_eq!(value.fog_color, [10, 20, 30]);
    assert!((value.fog_density_far - 0.9).abs() < 1e-12);
    assert_eq!(value.environment_image_filename, "fog.hdr");
}

fn check_global(data: &ClassObjectData) {
    let ClassObjectData::RenderGlobal(value) = data else {
        panic!("RENDERGLOBAL");
    };
    assert_eq!(value.procedure, 2);
    assert_eq!(value.save_filename, "out.png");
    assert_eq!(value.image_height, 600);
}

fn check_lights(data: &ClassObjectData, handle: u64) {
    let ClassObjectData::LightList(value) = data else {
        panic!("LIGHTLIST");
    };
    assert_eq!(value.class_version, 2);
    assert_eq!(value.lights.len(), 1);
    assert_eq!(value.lights[0].name, "spot");
    assert_eq!(value.lights[0].handle, Handle::new(handle));
}

fn check_sun(data: &ClassObjectData) {
    let ClassObjectData::Sun(value) = data else {
        panic!("SUN");
    };
    assert!(value.is_on);
    assert_eq!(value.color, Color::from_index(1));
    assert!((value.intensity - 1.5).abs() < 1e-12);
    assert_eq!(value.julian_day, 2451545);
    assert_eq!(value.shadow_map_size, 256);
    assert_eq!(value.shadow_softness, 3);
}

fn check_motion(data: &ClassObjectData, camera: u64, target: u64, view: u64) {
    let ClassObjectData::MotionPath(value) = data else {
        panic!("ACDBMOTIONPATH");
    };
    assert_eq!(value.class_version, 1);
    assert_eq!(value.camera_path, Handle::new(camera));
    assert_eq!(value.target_path, Handle::new(target));
    assert_eq!(value.view, Handle::new(view));
    assert_eq!(value.frames, 30);
    assert_eq!(value.frame_rate, 24);
    assert!(value.corner_deceleration);
}

fn check_curve(data: &ClassObjectData, entity: u64) {
    let ClassObjectData::CurvePath(value) = data else {
        panic!("ACDBCURVEPATH");
    };
    assert_eq!(value.entity, Handle::new(entity));
}

fn check_point(data: &ClassObjectData) {
    let ClassObjectData::PointPath(value) = data else {
        panic!("ACDBPOINTPATH");
    };
    assert_eq!(value.class_version, 1);
    assert_eq!(value.point, Vector3::new(1.25, 2.5, 3.75));
}

fn check_tv(data: &ClassObjectData) {
    let ClassObjectData::TvDeviceProperties(value) = data else {
        panic!("TVDEVICEPROPERTIES");
    };
    assert_eq!(value.flags, 7);
    assert_eq!(value.max_regen_threads, 4);
    assert_eq!(value.alternate_highlight, 11);
    assert!((value.antialiasing_level - 4.5).abs() < 1e-12);
}

fn check_filter(data: &ClassObjectData) {
    let ClassObjectData::LayerFilter(value) = data else {
        panic!("LAYERFILTER");
    };
    assert_eq!(value.names, vec!["Walls".to_string(), "Doors".to_string()]);
}

fn check_partial(data: &ClassObjectData, object: u64) {
    let ClassObjectData::PartialViewingIndex(value) = data else {
        panic!("PARTIAL_VIEWING_INDEX");
    };
    assert!(value.has_entries);
    assert_eq!(value.entries.len(), 1);
    assert_eq!(value.entries[0].extents_max, Vector3::new(10.0, 10.0, 1.0));
    assert_eq!(value.entries[0].object, Handle::new(object));
}

fn check_section_settings(data: &ClassObjectData, source: u64, destination: u64) {
    let ClassObjectData::SectionSettings(value) = data else {
        panic!("SECTION_SETTINGS");
    };
    assert_eq!(value.current_type, 1);
    assert_eq!(value.types.len(), 1);
    assert_eq!(value.types[0].generation, 2);
    assert_eq!(value.types[0].sources, vec![Handle::new(source)]);
    assert_eq!(value.types[0].destination_block, Handle::new(destination));
    assert_eq!(value.types[0].destination_file, "out.dwg");
    assert_eq!(value.types[0].geometry.len(), 1);
    assert_eq!(value.types[0].geometry[0].color, Color::from_index(1));
    assert_eq!(value.types[0].geometry[0].hatch_pattern, "ANSI31");
    assert!((value.types[0].geometry[0].hatch_scale - 1.25).abs() < 1e-12);
}

fn check_study(data: &ClassObjectData, wizard: u64, view: u64, visual: u64, text: u64) {
    let ClassObjectData::SunStudy(value) = data else {
        panic!("SUNSTUDY");
    };
    assert_eq!(value.setup_name, "Summer");
    assert!(value.use_subset);
    assert_eq!(value.sheet_set_name, "Set");
    assert_eq!(value.dates.len(), 1);
    assert_eq!(value.dates[0].julian_day, 2451545);
    assert!(value.select_range_of_dates);
    assert_eq!(value.start_time, 8);
    assert_eq!(value.hours, vec![true, false, true]);
    assert!((value.spacing - 0.5).abs() < 1e-12);
    assert_eq!(value.page_setup_wizard, Handle::new(wizard));
    assert_eq!(value.view, Handle::new(view));
    assert_eq!(value.visual_style, Handle::new(visual));
    assert_eq!(value.text_style, Handle::new(text));
}

fn check_table(data: &ClassObjectData, handle: u64) {
    let ClassObjectData::DataTable(value) = data else {
        panic!("DATATABLE");
    };
    assert_eq!(value.flags, 1);
    assert_eq!(value.name, "Schedule");
    assert_eq!(value.row_count, 1);
    assert_eq!(value.columns.len(), 2);
    assert_eq!(value.columns[0].rows[0].integer, 7);
    assert_eq!(value.columns[1].rows[0].handle, Handle::new(handle));
}

fn check_geo(data: &ClassObjectData) {
    let ClassObjectData::GeoMapImage(value) = data else {
        panic!("GEOMAPIMAGE");
    };
    assert_eq!(value.origin, Vector3::new(1.0, 2.0, 3.0));
    assert_eq!(value.image_size, Vector2::new(64.0, 32.0));
    assert!(value.clipping_enabled);
    assert_eq!(value.brightness, 10);
    assert_eq!(value.fade, 30);
}

fn check_base(value: &ModelDocViewStyle, description: &str, display: &str, style_base: bool) {
    assert_eq!(value.class_version, 1);
    assert_eq!(value.description, description);
    assert!(value.modified_for_recompute);
    if style_base {
        assert_eq!(value.display_name, display);
        assert_eq!(value.flags, 2);
    } else {
        assert_eq!(value.display_name, "");
        assert_eq!(value.flags, 0);
    }
}

fn check_detail(
    data: &ClassObjectData,
    identifier: u64,
    arrow: u64,
    boundary: u64,
    label: u64,
    connection: u64,
    border: u64,
    style_base: bool,
) {
    let ClassObjectData::DetailViewStyle(value) = data else {
        panic!("ACDBDETAILVIEWSTYLE");
    };
    check_base(&value.base, "Detail style", "Detail", style_base);
    assert_eq!(value.class_version, 1);
    assert_eq!(value.flags, 3);
    assert_eq!(value.identifier_style, Handle::new(identifier));
    assert_eq!(value.identifier_color, Color::from_index(1));
    assert!((value.identifier_height - 2.5).abs() < 1e-12);
    assert_eq!(value.identifier_excluded_characters, "IOS");
    assert!((value.identifier_offset - 0.25).abs() < 1e-12);
    assert!((value.arrow_symbol_size - 1.5).abs() < 1e-12);
    assert_eq!(value.arrow_symbol, Handle::new(arrow));
    assert_eq!(value.boundary_linetype, Handle::new(boundary));
    assert_eq!(value.view_label_text_style, Handle::new(label));
    assert_eq!(value.connection_linetype, Handle::new(connection));
    assert_eq!(value.border_linetype, Handle::new(border));
    assert_eq!(value.view_label_pattern, "%<VIEW>");
    assert_eq!(value.model_edge, 2);
}

fn check_section_style(
    data: &ClassObjectData,
    identifier: u64,
    start: u64,
    end: u64,
    plane: u64,
    bend: u64,
    label: u64,
    style_base: bool,
) {
    let ClassObjectData::SectionViewStyle(value) = data else {
        panic!("ACDBSECTIONVIEWSTYLE");
    };
    check_base(&value.base, "Section style", "Section", style_base);
    assert_eq!(value.class_version, 1);
    assert_eq!(value.flags, 3);
    assert_eq!(value.identifier_style, Handle::new(identifier));
    assert_eq!(value.arrow_start_symbol, Handle::new(start));
    assert_eq!(value.arrow_end_symbol, Handle::new(end));
    assert_eq!(value.plane_linetype, Handle::new(plane));
    assert_eq!(value.bend_linetype, Handle::new(bend));
    assert_eq!(value.view_label_text_style, Handle::new(label));
    assert_eq!(value.hatch_color, Color::from_index(6));
    assert_eq!(value.hatch_background_color, Color::from_index(7));
    assert_eq!(value.hatch_pattern, "ANSI31");
    assert!((value.hatch_scale - 1.25).abs() < 1e-12);
    assert!(value.reserved_flags[0] && !value.reserved_flags[1]);
    assert_eq!(value.identifier_position, 1);
    assert!((value.identifier_offset - 0.25).abs() < 1e-12);
    assert_eq!(value.arrow_position, 2);
    assert!((value.end_line_overshoot - 0.5).abs() < 1e-12);
    assert_eq!(value.hatch_angles.len(), 1);
    assert!((value.hatch_angles[0] - 45.0).abs() < 1e-12);
}

fn check_link(data: &ClassObjectData, target: u64, owner: u64, frames: bool, stolen_end: bool) {
    let ClassObjectData::DataLink(value) = data else {
        panic!("DATALINK");
    };
    assert_eq!(value.data_adapter, "AcExcel");
    assert_eq!(value.description, "Schedule");
    assert_eq!(value.option, 1);
    assert_eq!(value.year, 2024);
    assert_eq!(value.custom_data.len(), 1);
    assert_eq!(value.custom_data[0].target, Handle::new(target));
    assert_eq!(
        value.custom_data[0].value,
        if stolen_end { "ACVALUE_END" } else { "" }
    );
    assert_eq!(value.hard_owner, Handle::new(owner));
    assert_eq!(value.data_map.len(), 2);
    assert_eq!(value.data_map[0].name, "x");
    assert_eq!(value.data_map[0].value.text, "hello");
    assert_eq!(value.data_map[1].name, "y");
    assert!((value.data_map[1].value.numeric_value - 12.5).abs() < 1e-12);
    if frames {
        assert_eq!(value.data_map[0].value.formatted_value, "hello");
        assert_eq!(value.data_map[1].value.formatted_value, "12.5");
    }
}

fn check_ird_dict(data: &ClassObjectData, table: u64, extra: u64) {
    let ClassObjectData::ObjectDataDictionary(value) = data else {
        panic!("IRD_DSC_DICT");
    };
    assert_eq!(value.duplicate_cloning, 1);
    assert!(!value.hard_owner);
    assert_eq!(value.entries.len(), 2);
    assert_eq!(value.entries[0].0, "*A1");
    assert_eq!(value.entries[0].1, Handle::new(table));
    assert_eq!(value.entries[1].0, "*A2");
    assert_eq!(value.entries[1].1, Handle::new(extra));
}

fn check_ird_desc(data: &ClassObjectData) {
    let ClassObjectData::ObjectDataDescriptor(value) = data else {
        panic!("IRD_DSC_RECORD");
    };
    assert_eq!(value.kind, 0x3000);
    assert_eq!(value.name, "TEST-_-ade");
    let table = value.as_table().expect("table");
    assert_eq!(table.name, "TEST");
    assert_eq!(table.fields.len(), 4);
    assert_eq!(table.fields[0].name, "VOLTAGE");
    assert_eq!(table.fields[2].name, "OWNER");
}

fn near(a: f64, b: f64) {
    assert!((a - b).abs() < 1e-12, "{a} != {b}");
}

fn viewrep_h(remap: bool, dxf: u64, dwg: u64) -> Handle {
    Handle::new(if remap { dwg } else { dxf })
}

fn check_view_rep_guid(value: &acadrust::objects::ViewRepGuid) {
    assert_eq!(value.data1, 0x1234_5678);
    assert_eq!(value.data2, 0x1111);
    assert_eq!(value.data3, 0x2222);
    assert_eq!(value.data4, [1, 2, 3, 4, 5, 6, 7, 8]);
}

fn check_view_rep_transform(value: &[f64; 16]) {
    let expected = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 10.0, 20.0, 30.0, 1.0];
    for (got, want) in value.iter().zip(expected) {
        near(*got, want);
    }
}

fn check_viewrep(doc: &acadrust::CadDocument, remap: bool) {
    for name in [
        "ACDBVIEWREPSOURCEMGR",
        "ACDBVIEWREPSTANDARD",
        "ACDBVIEWREPORIENTATIONDEF",
        "ACDBVIEWREPORIENTATION",
        "ACDBVIEWREPSECTIONDEFINITION",
        "ACDBSYMODELSPACEVIEWSELSET",
        "ACDBVIEWREPMODELSPACESOURCE",
        "ACDBVIEWREP",
    ] {
        check_class(doc, name);
    }
    let ClassObjectData::ViewRepSourceManager(value) = class_data(doc, "ACDBVIEWREPSOURCEMGR") else {
        panic!("ACDBVIEWREPSOURCEMGR");
    };
    assert!(value.has_source);
    assert_eq!(value.source, viewrep_h(remap, 0xE501, 0xF19));
    assert_eq!(value.status, 1);

    let ClassObjectData::ViewRepStandard(value) = class_data(doc, "ACDBVIEWREPSTANDARD") else {
        panic!("ACDBVIEWREPSTANDARD");
    };
    assert_eq!(value.values, [1, 2, 3, 4, 5, 6]);

    assert!(matches!(
        class_data(doc, "ACDBVIEWREPORIENTATIONDEF"),
        ClassObjectData::ViewRepOrientationDefinition
    ));

    let ClassObjectData::ViewRepOrientation(value) = class_data(doc, "ACDBVIEWREPORIENTATION") else {
        panic!("ACDBVIEWREPORIENTATION");
    };
    assert_eq!(value.camera, Vector3::new(0.0, 0.0, 100.0));
    assert_eq!(value.target, Vector3::new(0.0, 0.0, 0.0));
    assert_eq!(value.normal, Vector3::new(0.0, 1.0, 0.0));

    let ClassObjectData::ViewRepSectionDefinition(value) = class_data(doc, "ACDBVIEWREPSECTIONDEFINITION")
    else {
        panic!("ACDBVIEWREPSECTIONDEFINITION");
    };
    assert_eq!(value.version, 1);
    near(value.section_depth, 25.5);
    assert_eq!(value.flags, [2, 3]);

    let ClassObjectData::ViewRepModelSpaceViewSelectionSet(value) =
        class_data(doc, "ACDBSYMODELSPACEVIEWSELSET")
    else {
        panic!("ACDBSYMODELSPACEVIEWSELSET");
    };
    assert_eq!(value.version, 1);
    assert_eq!(value.entities, vec![viewrep_h(remap, 0xE502, 0xF1A)]);

    let ClassObjectData::ViewRepModelSpaceSource(value) = class_data(doc, "ACDBVIEWREPMODELSPACESOURCE")
    else {
        panic!("ACDBVIEWREPMODELSPACESOURCE");
    };
    assert!(value.enabled);
    assert_eq!(value.header_values, [1, 2, 3, 4, 5, 6]);
    check_view_rep_transform(&value.transform);
    assert_eq!(value.source_version, 1);
    assert_eq!(value.source_status, 2);
    assert_eq!(value.model, viewrep_h(remap, 0xE503, 0xF1B));
    check_view_rep_guid(&value.guid);
    assert_eq!(value.references, [viewrep_h(remap, 0xE504, 0xF1C), viewrep_h(remap, 0xE505, 0xF1D)]);
    assert_eq!(value.tail_values, [3, 4]);
    assert_eq!(value.orientation, viewrep_h(remap, 0xE506, 0xF1E));

    let ClassObjectData::ViewRep(value) = class_data(doc, "ACDBVIEWREP") else {
        panic!("ACDBVIEWREP");
    };
    assert_eq!(value.header_values, [1, 2, 3, 4, 5]);
    assert_eq!(value.name, "Front");
    assert_eq!(value.scale, 2);
    assert_eq!(value.header_status, 1);
    assert_eq!(value.description, "view");
    assert_eq!(value.source_id, 11);
    assert!(value.source_enabled);
    assert_eq!(value.source_version, 1);
    assert_eq!(value.model_id, 22);
    check_view_rep_guid(&value.guid);
    assert_eq!(value.marker, 9);
    check_view_rep_transform(&value.transform);
    assert_eq!(value.transform_version, 1);
    assert_eq!(value.database_id, 33);
    assert_eq!(value.geometry_version, 1);
    assert_eq!(value.sketches.len(), 2);
    assert_eq!(value.sketches[0].id, 7);
    assert!(value.sketches[0].enabled);
    assert_eq!(value.sketches[0].references[0].object, viewrep_h(remap, 0xE507, 0xF1F));
    assert!(value.sketches[0].references[0].flag);
    match &value.sketches[0].geometry {
        ViewRepSketchGeometry::Line {
            type_code,
            first,
            second,
        } => {
            assert_eq!(*type_code, 19);
            assert_eq!(*first, Vector3::new(0.0, 0.0, 0.0));
            assert_eq!(*second, Vector3::new(10.0, 0.0, 0.0));
        }
        other => panic!("{other:?}"),
    }
    match &value.sketches[1].geometry {
        ViewRepSketchGeometry::Nurb {
            type_code,
            flags,
            degree,
            tolerance,
            knot_header,
            knots,
            weight_header,
            weights,
            point_header,
            control_points,
        } => {
            assert_eq!(*type_code, 42);
            assert_eq!(*flags, [true, false]);
            assert_eq!(*degree, 3);
            near(*tolerance, 0.01);
            assert_eq!(*knot_header, [2, 0, 0]);
            assert_eq!(knots, &vec![0.0, 1.0]);
            assert_eq!(*weight_header, [2, 0, 0]);
            assert_eq!(weights, &vec![1.0, 1.0]);
            assert_eq!(*point_header, [2, 0, 0]);
            assert_eq!(
                control_points,
                &vec![Vector3::new(0.0, 0.0, 0.0), Vector3::new(1.0, 1.0, 0.0)]
            );
        }
        other => panic!("{other:?}"),
    }
    assert_eq!(
        value.related_objects,
        [viewrep_h(remap, 0xE508, 0xF20), viewrep_h(remap, 0xE509, 0xF21)]
    );
    assert_eq!(value.source_manager, viewrep_h(remap, 0xE50A, 0xF22));
    assert_eq!(
        value.owned_objects,
        [viewrep_h(remap, 0xE50B, 0xF23), viewrep_h(remap, 0xE50C, 0xF24)]
    );
    assert_eq!(
        value.optional_objects,
        [viewrep_h(remap, 0xE50D, 0xF25), viewrep_h(remap, 0xE50E, 0xF26)]
    );
    near(value.position.x, 100.0);
    near(value.position.y, 200.0);
    near(value.rotation, 90.0);
    assert_eq!(value.orientation, viewrep_h(remap, 0xE50F, 0xF27));
    assert!(value.is_active);
    assert_eq!(value.projection, 1);
    assert_eq!(
        value.linked_views,
        [viewrep_h(remap, 0xE510, 0xF28), viewrep_h(remap, 0xE511, 0xF29)]
    );
    assert_eq!(value.section_sketches.len(), 1);
    assert_eq!(value.section_sketches[0].class_name, "AcDbViewRepSketch");
    assert_eq!(
        value.section_sketches[0].objects,
        vec![viewrep_h(remap, 0xE512, 0xF2A), viewrep_h(remap, 0xE513, 0xF2B)]
    );
    assert_eq!(value.action_mode, 1);
    assert_eq!(value.action, Some(viewrep_h(remap, 0xE514, 0xF2C)));
    assert!(value.has_parent);
    assert_eq!(value.parent, viewrep_h(remap, 0xE515, 0xF2D));
    assert_eq!(value.tail_version, 1);
    assert_eq!(value.tail_state, 2);
    assert_eq!(value.tail_id, 44);
    assert_eq!(value.path_count, 1);
    assert_eq!(value.path_version, 1);
    assert_eq!(value.path_id, 55);
    assert!(value.has_block_path);
    let path = value.block_path.as_ref().expect("block path");
    assert_eq!(path.class_name, "AcDbBlockReferencePath");
    assert_eq!(path.version, 1);
    assert_eq!(path.entries.len(), 1);
    assert_eq!(path.entries[0].flag, 1);
    assert_eq!(path.entries[0].kind, 2);
    assert_eq!(path.entries[0].object, viewrep_h(remap, 0xE516, 0xF2E));
    assert_eq!(value.style, viewrep_h(remap, 0xE517, 0xF2F));
}

fn check_ird_record(data: &ClassObjectData, table: u64) {
    let ClassObjectData::ObjectDataRecord(value) = data else {
        panic!("IRD_OBJ_RECORD");
    };
    assert_eq!(value.table, Handle::new(table));
    let fields = vec![
        ObjectDataField::new("VOLTAGE", ObjectDataType::Real),
        ObjectDataField::new("CIRCUITS", ObjectDataType::Integer),
        ObjectDataField::new("OWNER", ObjectDataType::Character),
        ObjectDataField::new("ENERGIZED", ObjectDataType::Integer),
    ];
    assert_eq!(
        value.values(&fields),
        Some(vec![
            ObjectDataValue::Real(230.0),
            ObjectDataValue::Integer(1),
            ObjectDataValue::Character("hello".into()),
            ObjectDataValue::Integer(1),
        ])
    );
}

fn check_scene(
    doc: &acadrust::CadDocument,
    image_xy: bool,
    style_base: bool,
    ibl: u64,
    sky: u64,
    lights: u64,
    camera: u64,
    target: u64,
    view: u64,
    curve: u64,
    partial: u64,
    source: u64,
    destination: u64,
    wizard: u64,
    study_view: u64,
    visual: u64,
    text: u64,
    table: u64,
    detail_identifier: u64,
    detail_arrow: u64,
    detail_boundary: u64,
    detail_label: u64,
    detail_connection: u64,
    detail_border: u64,
    section_identifier: u64,
    section_start: u64,
    section_end: u64,
    section_plane: u64,
    section_bend: u64,
    section_label: u64,
    frames: bool,
    stolen_end: bool,
    link_target: u64,
    link_owner: u64,
    ird_table: u64,
    ird_extra: u64,
) {
    for name in [
        "GRADIENT_BACKGROUND",
        "GROUND_PLANE_BACKGROUND",
        "RAPIDRTRENDERENVIRONMENT",
        "IMAGE_BACKGROUND",
        "SKYLIGHT_BACKGROUND",
        "SOLID_BACKGROUND",
        "RENDERENTRY",
        "RENDERENVIRONMENT",
        "RENDERGLOBAL",
        "LIGHTLIST",
        "SUN",
        "ACDBMOTIONPATH",
        "ACDBCURVEPATH",
        "ACDBPOINTPATH",
        "TVDEVICEPROPERTIES",
        "LAYERFILTER",
        "PARTIAL_VIEWING_INDEX",
        "SECTION_SETTINGS",
        "SUNSTUDY",
        "DATATABLE",
        "GEOMAPIMAGE",
        "ACDBDETAILVIEWSTYLE",
        "ACDBSECTIONVIEWSTYLE",
        "DATALINK",
        "IRD_DSC_DICT",
        "IRD_DSC_RECORD",
        "IRD_OBJ_RECORD",
        "ACDBVIEWREPSOURCEMGR",
        "ACDBVIEWREPSTANDARD",
        "ACDBVIEWREPORIENTATIONDEF",
        "ACDBVIEWREPORIENTATION",
        "ACDBVIEWREPSECTIONDEFINITION",
        "ACDBSYMODELSPACEVIEWSELSET",
        "ACDBVIEWREPMODELSPACESOURCE",
        "ACDBVIEWREP",
    ] {
        check_class(doc, name);
    }
    check_gradient(class_data(doc, "GRADIENT_BACKGROUND"));
    check_ground(class_data(doc, "GROUND_PLANE_BACKGROUND"));
    check_ibl(class_data(doc, "RAPIDRTRENDERENVIRONMENT"), ibl);
    check_image(class_data(doc, "IMAGE_BACKGROUND"), image_xy);
    check_sky(class_data(doc, "SKYLIGHT_BACKGROUND"), sky);
    check_solid(class_data(doc, "SOLID_BACKGROUND"));
    check_entry(class_data(doc, "RENDERENTRY"));
    check_environment(class_data(doc, "RENDERENVIRONMENT"));
    check_global(class_data(doc, "RENDERGLOBAL"));
    check_lights(class_data(doc, "LIGHTLIST"), lights);
    check_sun(class_data(doc, "SUN"));
    check_motion(class_data(doc, "ACDBMOTIONPATH"), camera, target, view);
    check_curve(class_data(doc, "ACDBCURVEPATH"), curve);
    check_point(class_data(doc, "ACDBPOINTPATH"));
    check_tv(class_data(doc, "TVDEVICEPROPERTIES"));
    check_filter(class_data(doc, "LAYERFILTER"));
    check_partial(class_data(doc, "PARTIAL_VIEWING_INDEX"), partial);
    check_section_settings(class_data(doc, "SECTION_SETTINGS"), source, destination);
    check_study(class_data(doc, "SUNSTUDY"), wizard, study_view, visual, text);
    check_table(class_data(doc, "DATATABLE"), table);
    check_geo(class_data(doc, "GEOMAPIMAGE"));
    check_detail(
        class_data(doc, "ACDBDETAILVIEWSTYLE"),
        detail_identifier,
        detail_arrow,
        detail_boundary,
        detail_label,
        detail_connection,
        detail_border,
        style_base,
    );
    check_section_style(
        class_data(doc, "ACDBSECTIONVIEWSTYLE"),
        section_identifier,
        section_start,
        section_end,
        section_plane,
        section_bend,
        section_label,
        style_base,
    );
    check_link(
        class_data(doc, "DATALINK"),
        link_target,
        link_owner,
        frames,
        stolen_end,
    );
    check_ird_dict(class_data(doc, "IRD_DSC_DICT"), ird_table, ird_extra);
    check_ird_desc(class_data(doc, "IRD_DSC_RECORD"));
    check_ird_record(class_data(doc, "IRD_OBJ_RECORD"), ird_table);
}

fn check_dxf(doc: &acadrust::CadDocument, predefined: bool) {
    check_class(doc, "RENDERSETTINGS");
    check_class(doc, "RAPIDRTRENDERSETTINGS");
    check_class(doc, "MENTALRAYRENDERSETTINGS");
    check_class(doc, "SECTION_MANAGER");
    check_render(class_data(doc, "RENDERSETTINGS"), predefined);
    check_rapid(class_data(doc, "RAPIDRTRENDERSETTINGS"));
    check_mental(class_data(doc, "MENTALRAYRENDERSETTINGS"));
    check_sections(class_data(doc, "SECTION_MANAGER"), 0x1234);
    check_scene(
        doc,
        false,
        true,
        0x3456,
        0x4567,
        0x2345,
        0x5678,
        0x6789,
        0x789A,
        0x89AB,
        0xBCDE,
        0x9ABC,
        0xABCD,
        0xCDEF,
        0xD012,
        0xD123,
        0xD234,
        0xD345,
        0xE101,
        0xE102,
        0xE103,
        0xE104,
        0xE105,
        0xE106,
        0xE201,
        0xE202,
        0xE203,
        0xE204,
        0xE205,
        0xE206,
        true,
        true,
        0xE301,
        0xE302,
        0xE401,
        0xE402,
    );
    check_viewrep(doc, false);
}

pub fn verify(out: &str) -> acadrust::Result<()> {
    let ascii = dxf(&format!("{out}-ascii.dxf"));
    assert_eq!(ascii.version, DxfVersion::AC1032);
    check_dxf(&ascii, true);
    check_dxf(&dxf(&format!("{out}-binary.dxf")), true);

    for version in [
        DxfVersion::AC1018,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let tag = version.as_str();
        for suffix in ["ascii", "binary"] {
            let doc = dxf(&format!("{out}-{tag}-{suffix}.dxf"));
            assert_eq!(doc.version, version, "{tag} {suffix}");
            check_render(
                class_data(&doc, "RENDERSETTINGS"),
                version >= DxfVersion::AC1027,
            );
            check_rapid(class_data(&doc, "RAPIDRTRENDERSETTINGS"));
            check_mental(class_data(&doc, "MENTALRAYRENDERSETTINGS"));
            check_sections(class_data(&doc, "SECTION_MANAGER"), 0x1234);
            check_scene(
                &doc,
                false,
                version >= DxfVersion::AC1032,
                0x3456,
                0x4567,
                0x2345,
                0x5678,
                0x6789,
                0x789A,
                0x89AB,
                0xBCDE,
                0x9ABC,
                0xABCD,
                0xCDEF,
                0xD012,
                0xD123,
                0xD234,
                0xD345,
                0xE101,
                0xE102,
                0xE103,
                0xE104,
                0xE105,
                0xE106,
                0xE201,
                0xE202,
                0xE203,
                0xE204,
                0xE205,
                0xE206,
                version >= DxfVersion::AC1021,
                version >= DxfVersion::AC1021,
                0xE301,
                0xE302,
                0xE401,
                0xE402,
            );
            check_viewrep(&doc, false);
        }
    }

    let restored = dwg(&format!("{out}.dwg"));
    assert_eq!(restored.version, DxfVersion::AC1032);
    check_class(&restored, "RENDERSETTINGS");
    check_render(class_data(&restored, "RENDERSETTINGS"), true);
    check_rapid(class_data(&restored, "RAPIDRTRENDERSETTINGS"));
    check_mental(class_data(&restored, "MENTALRAYRENDERSETTINGS"));
    check_sections(class_data(&restored, "SECTION_MANAGER"), 0xABC);
    check_scene(
        &restored,
        true,
        true,
        0xCDE,
        0xDEF,
        0xBCD,
        0xE01,
        0xE02,
        0xE03,
        0xE04,
        0xF03,
        0xF01,
        0xF02,
        0xF04,
        0xF05,
        0xF06,
        0xF07,
        0xF08,
        0xF09,
        0xF0A,
        0xF0B,
        0xF0C,
        0xF0D,
        0xF0E,
        0xF0F,
        0xF10,
        0xF11,
        0xF12,
        0xF13,
        0xF14,
        true,
        false,
        0xF15,
        0xF16,
        0xF17,
        0xF18,
    );
    check_viewrep(&restored, true);

    let ac1027 = dwg(&format!("{out}-ac1027.dwg"));
    assert_eq!(ac1027.version, DxfVersion::AC1027);
    check_rapid(class_data(&ac1027, "RAPIDRTRENDERSETTINGS"));
    Ok(())
}
