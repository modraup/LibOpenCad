// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::underlay::{UnderlayDefinition, UnderlayDisplayFlags, UnderlayType};
use acadrust::entities::{Shape, Underlay};
use acadrust::objects::ObjectType;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let mut style = acadrust::tables::TextStyle::new("SHAPES");
    style.is_shape_file = true;
    style.font_file = "中文形.shx".into();
    doc.text_styles.add(style)?;
    let root = doc.header.named_objects_dict_handle;
    let mut definitions = vec![];
    for (i, kind) in [UnderlayType::Pdf, UnderlayType::Dwf, UnderlayType::Dgn]
        .into_iter()
        .enumerate()
    {
        let mut def = UnderlayDefinition::new(kind);
        def.handle = doc.allocate_handle();
        def.owner_handle = root;
        def.file_path = format!("底图{i}.dat");
        def.page_name = "页面一".into();
        def.unloaded = true;
        let handle = def.handle;
        doc.objects
            .insert(handle, ObjectType::UnderlayDefinition(def));
        let ObjectType::Dictionary(dict) = doc.objects.get_mut(&root).unwrap() else {
            panic!()
        };
        dict.add_entry(format!("底图{i}"), handle);
        definitions.push((kind, handle));
    }
    for normal in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., -2., 3.),
        Vector3::ZERO,
        Vector3::new(1e-14, 0., 1e-14),
    ] {
        for (i, oblique) in [
            0.,
            0.25,
            std::f64::consts::FRAC_PI_2,
            std::f64::consts::FRAC_PI_2 - 1e-13,
        ]
        .into_iter()
        .enumerate()
        {
            let mut s = Shape::with_name(Vector3::new(1., 2., 3.), "符号", 4.);
            s.normal = normal;
            s.rotation = if i == 0 { 0. } else { 0.7 };
            s.relative_x_scale = if i % 2 == 0 { -2. } else { 2. };
            s.oblique_angle = oblique;
            s.thickness = -3.;
            doc.add_entity(EntityType::Shape(s))?;
        }
        for (kind, handle) in &definitions {
            for negative in [false, true] {
                let mut u = Underlay::new(*kind);
                u.definition_handle = *handle;
                u.insertion_point = Vector3::new(2., 3., 4.);
                u.normal = normal;
                u.rotation = 0.37;
                u.x_scale = if negative { -2. } else { 2. };
                u.y_scale = 3.;
                u.z_scale = if negative { -4. } else { 4. };
                u.flags = UnderlayDisplayFlags::from_bits_truncate(19);
                u.contrast = 75;
                u.fade = 20;
                u.clip_inverted = negative;
                u.clip_boundary_vertices = vec![
                    Vector2::new(-1., 2.),
                    Vector2::new(4., -3.),
                    Vector2::new(5., 6.),
                ];
                doc.add_entity(EntityType::Underlay(u))?;
            }
        }
    }
    use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
    for e in doc.entities_mut() {
        e.common_mut().extended_data.add_record(ExtendedDataRecord {
            application_name: "Geometry".into(),
            values: vec![
                X::Position3D(Vector3::new(1., 2., 3.)),
                X::Direction3D(Vector3::UNIT_Y),
                X::Distance(2.5),
            ],
        });
    }
    let mut controls = vec![doc
        .entities()
        .find(|e| matches!(e, EntityType::Shape(_)))
        .unwrap()
        .clone()];
    for (kind, _) in &definitions {
        controls.push(
            doc.entities()
                .find(|e| matches!(e,EntityType::Underlay(u) if u.underlay_type==*kind))
                .unwrap()
                .clone(),
        );
    }
    for mut e in controls {
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        doc.add_paper_space_entity(e)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn values(e: &EntityType) -> Vec<f64> {
    let mut result = vec![];
    fn p(v: &mut Vec<f64>, p: Vector3) {
        v.extend([p.x, p.y, p.z]);
    }
    match e {
        EntityType::Shape(s) => {
            p(&mut result, s.insertion_point);
            p(&mut result, s.normal);
            result.extend([
                s.size,
                s.rotation,
                s.relative_x_scale,
                s.oblique_angle,
                s.thickness,
            ]);
        }
        EntityType::Underlay(u) => {
            p(&mut result, u.insertion_point);
            p(&mut result, u.normal);
            result.extend([u.x_scale, u.y_scale, u.z_scale, u.rotation]);
            let clip = u.world_clip_boundary();
            result.push(clip.len() as f64);
            for q in clip {
                p(&mut result, q);
            }
        }
        _ => panic!(),
    }
    result
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(point($a.$field,$b.$field);$a.$field=$b.$field;)*}}
macro_rules! numbers{($a:ident,$b:ident,$($field:ident),*)=>{$(near($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Shape(a), EntityType::Shape(b)) => {
            vectors!(a, b, insertion_point, normal);
            numbers!(
                a,
                b,
                size,
                rotation,
                relative_x_scale,
                oblique_angle,
                thickness
            );
        }
        (EntityType::Underlay(a), EntityType::Underlay(b)) => {
            vectors!(a, b, insertion_point, normal);
            numbers!(a, b, x_scale, y_scale, z_scale, rotation);
        }
        _ => panic!("Wrong shape/underlay type"),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 64);
    for step in 0..16 {
        eprintln!("shape/underlay step {step}");
        let t = crate::entity_transform::operation(if step == 14 {
            4
        } else if step == 15 {
            6
        } else {
            step
        });
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" {
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
        assert_eq!(rows.lines().count(), 64);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let handle = p.next().unwrap();
            let h = Handle::new(u64::from_str_radix(handle, 16).unwrap());
            let a: Vec<f64> = p.map(|p| p.parse().unwrap()).collect();
            let b = values(expected.get_entity(h).unwrap());
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
        assert_eq!(actual.entities().count(), 64);
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.text_styles, actual.text_styles);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
