// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::Circle;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.layers.add(acadrust::Layer::new("冻结图层"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    doc.ucss.add(acadrust::tables::Ucs::new("用户坐标"))?;
    let ucs = doc.ucss.get("用户坐标").unwrap().handle;
    let frozen = doc.layers.get("冻结图层").unwrap().handle;
    for name in ["图纸二", "Sheet3"] {
        doc.add_layout(name)?;
    }
    for e in doc.entities_mut() {
        e.common_mut().layer = "Control".into();
    }
    for name in ["图纸二", "Sheet3"] {
        let clip = doc.add_entity_to_layout(
            EntityType::Circle(Circle::from_center_radius(
                Vector3::new(100., 120., 0.),
                80.,
            )),
            name,
        )?;
        for (i, dir) in [
            Vector3::UNIT_Z,
            Vector3::new(1., -2., 3.),
            Vector3::ZERO,
            Vector3::new(1e-14, 0., 1e-14),
            -Vector3::UNIT_Y,
        ]
        .into_iter()
        .enumerate()
        {
            for off in [false, true] {
                let mut v = crate::layouts::viewport();
                v.center = Vector3::new(100. + i as f64, 120., 2.);
                v.view_direction = dir;
                v.ucs_x_axis = if i == 2 {
                    Vector3::ZERO
                } else {
                    Vector3::new(2., 1., 0.)
                };
                v.ucs_y_axis = if i == 3 {
                    Vector3::new(0., 1e-14, 0.)
                } else {
                    Vector3::new(1., 3., 1.)
                };
                v.view_height = 25.;
                v.lens_length = 37.;
                v.front_clip_z = 1.25;
                v.back_clip_z = -9.5;
                v.style_sheet = "样式.ctb".into();
                v.ucs_handle = ucs;
                v.base_ucs_handle = ucs;
                v.frozen_layers = vec![frozen];
                v.clip_boundary_handle = clip;
                v.status.non_rectangular_clipping = true;
                v.status.perspective = true;
                v.status.front_clipping = true;
                v.status.back_clipping = true;
                v.status.is_off = off;
                v.status.is_on = !off;
                v.off_screen = !off;
                v.dxf_status = Some(if off { 0 } else { -1 });
                use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
                v.common.extended_data.add_record(ExtendedDataRecord {
                    application_name: "Geometry".into(),
                    values: vec![
                        X::Position3D(Vector3::new(1., 2., 3.)),
                        X::Direction3D(Vector3::UNIT_Y),
                        X::Distance(2.5),
                    ],
                });
                doc.add_entity_to_layout(EntityType::Viewport(Box::new(v)), name)?;
            }
        }
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn values(v: &acadrust::entities::Viewport) -> Vec<f64> {
    let mut a = vec![];
    for p in [
        v.center,
        v.view_direction,
        v.view_target,
        v.ucs_origin,
        v.ucs_x_axis,
        v.ucs_y_axis,
        v.view_center,
        v.snap_base,
        v.snap_spacing,
        v.grid_spacing,
    ] {
        a.extend([p.x, p.y, p.z]);
    }
    a.extend([
        v.width,
        v.height,
        v.lens_length,
        v.front_clip_z,
        v.back_clip_z,
        v.view_height,
        v.snap_angle,
        v.twist_angle,
        v.elevation,
        v.brightness,
        v.contrast,
    ]);
    a
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(point($a.$field,$b.$field);$a.$field=$b.$field;)*}}
macro_rules! numbers{($a:ident,$b:ident,$($field:ident),*)=>{$(near($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    let (EntityType::Viewport(a), EntityType::Viewport(b)) = (a, b) else {
        panic!()
    };
    vectors!(
        a,
        b,
        center,
        view_direction,
        view_target,
        ucs_origin,
        ucs_x_axis,
        ucs_y_axis
    );
    numbers!(a, b, width, height);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let count = source
        .entities()
        .filter(|e| matches!(e, EntityType::Viewport(_)))
        .count();
    assert_eq!(
        source
            .entities()
            .filter(|e| matches!(e, EntityType::Viewport(_)) && e.common().layer != "Control")
            .count(),
        20
    );
    assert!(count >= 22);
    for step in 0..16 {
        eprintln!("viewport step {step}");
        let t = crate::entity_transform::operation(if step == 14 {
            4
        } else if step == 15 {
            6
        } else {
            step
        });
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" || !matches!(e, EntityType::Viewport(_)) {
                continue;
            }
            if step == 0 {
                e.translate(Vector3::new(3., -4., 5.));
            } else if [5, 6, 7, 11, 14, 15].contains(&step) {
                e.apply_mirror(&t);
                if step == 15 {
                    e.apply_mirror(&t);
                }
            } else {
                e.apply_transform(&t);
            }
        }
        let rows = std::fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        assert_eq!(rows.lines().count(), count);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let handle = p.next().unwrap();
            let h = acadrust::Handle::new(u64::from_str_radix(handle, 16).unwrap());
            let EntityType::Viewport(v) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let a: Vec<f64> = p.map(|p| p.parse().unwrap()).collect();
            let b = values(v);
            assert_eq!(a.len(), b.len());
            for (i, (a, b)) in a.iter().zip(&b).enumerate() {
                assert!(
                    (a - b).abs() <= 2e-10 * b.abs().max(1.),
                    "step {step} handle {handle} value {i}: {a} != {b}"
                );
            }
        }
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), source.entities().count());
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.ucss, actual.ucss);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
