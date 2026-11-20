// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::point;
use acadrust::entities::raster_image::ClipType;
use acadrust::entities::wipeout::{WipeoutClipMode, WipeoutClipType};
use acadrust::entities::{RasterImage, Wipeout};
use acadrust::objects::{ImageDefinition, ImageDefinitionReactor, ObjectType};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let root = doc.header.named_objects_dict_handle;
    let mut def = ImageDefinition::with_dimensions("图像.png", 64, 32);
    def.handle = doc.allocate_handle();
    def.owner = root;
    let handle = def.handle;
    doc.objects.insert(handle, ObjectType::ImageDefinition(def));
    let ObjectType::Dictionary(dict) = doc.objects.get_mut(&root).unwrap() else {
        panic!()
    };
    dict.add_entry("图片定义", handle);
    for (u, v) in [
        (Vector3::UNIT_X, Vector3::UNIT_Y),
        (Vector3::new(2., 1., 3.), Vector3::new(-1., 4., 2.)),
        (-Vector3::UNIT_X, Vector3::new(0., -2., 0.)),
        (Vector3::ZERO, Vector3::ZERO),
    ] {
        for (case, size) in [
            Vector2::new(64., 32.),
            Vector2::new(1., 1.),
            Vector2::new(-4., 3.),
            Vector2::new(0., 5.),
        ]
        .into_iter()
        .enumerate()
        {
            let boundary = match case {
                0 => vec![Vector2::new(-0.5, -0.5), Vector2::new(63.5, 31.5)],
                1 => vec![
                    Vector2::new(-1., -2.),
                    Vector2::new(4., -3.),
                    Vector2::new(2., 5.),
                ],
                2 => vec![],
                _ => vec![
                    Vector2::ZERO,
                    Vector2::new(3., 0.),
                    Vector2::new(0., 4.),
                    Vector2::ZERO,
                ],
            };
            let mut image = RasterImage::new("图像.png", Vector3::new(2., 3., 4.), size.x, size.y);
            image.definition_handle = Some(handle);
            image.u_vector = u;
            image.v_vector = v;
            image.brightness = 75;
            image.contrast = 65;
            image.fade = 20;
            image.clipping_enabled = case != 2;
            image.clip_boundary.clip_type = if case == 0 {
                ClipType::Rectangular
            } else {
                ClipType::Polygonal
            };
            image.clip_boundary.vertices = boundary.clone();
            doc.add_entity(EntityType::RasterImage(image))?;
            let mut w = Wipeout::new();
            w.insertion_point = Vector3::new(2., 3., 4.);
            w.u_vector = u;
            w.v_vector = v;
            w.size = size;
            w.clipping_enabled = case != 2;
            w.clip_type = if case == 0 {
                WipeoutClipType::Rectangular
            } else {
                WipeoutClipType::Polygonal
            };
            w.clip_mode = if case % 2 == 0 {
                WipeoutClipMode::Outside
            } else {
                WipeoutClipMode::Inside
            };
            w.clip_boundary_vertices = boundary;
            w.brightness = 75;
            w.contrast = 65;
            w.fade = 20;
            doc.add_entity(EntityType::Wipeout(w))?;
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
    let controls: Vec<_> = doc.entities().take(2).cloned().collect();
    for mut e in controls {
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        doc.add_paper_space_entity(e)?;
    }
    let images: Vec<_> = doc
        .entities()
        .filter(|e| matches!(e, EntityType::RasterImage(_)))
        .map(|e| e.common().handle)
        .collect();
    for h in images {
        let mut r = ImageDefinitionReactor::new(h);
        r.handle = doc.allocate_handle();
        r.owner = h;
        let rh = r.handle;
        doc.objects
            .insert(rh, ObjectType::ImageDefinitionReactor(r));
        let Some(EntityType::RasterImage(i)) = doc.get_entity_mut(h) else {
            panic!()
        };
        i.definition_reactor_handle = Some(rh);
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn values(e: &EntityType) -> Vec<f64> {
    let mut v = vec![];
    fn p(v: &mut Vec<f64>, p: Vector3) {
        v.extend([p.x, p.y, p.z]);
    }
    let (origin, u, w, size, corners, boundary) = match e {
        EntityType::RasterImage(i) => (
            i.insertion_point,
            i.u_vector,
            i.v_vector,
            i.size,
            i.corners(),
            &i.clip_boundary.vertices,
        ),
        EntityType::Wipeout(i) => (
            i.insertion_point,
            i.u_vector,
            i.v_vector,
            i.size,
            i.corners(),
            &i.clip_boundary_vertices,
        ),
        _ => panic!(),
    };
    p(&mut v, origin);
    p(&mut v, u);
    p(&mut v, w);
    v.extend([size.x, size.y]);
    for q in corners {
        p(&mut v, q);
    }
    let b = e.as_entity().bounding_box();
    p(&mut v, b.min);
    p(&mut v, b.max);
    v.push(boundary.len() as f64);
    for q in boundary {
        v.extend([q.x, q.y]);
    }
    if let EntityType::Wipeout(w) = e {
        for q in w.world_boundary_vertices() {
            p(&mut v, q);
        }
    }
    v
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(point($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::RasterImage(a), EntityType::RasterImage(b)) => {
            vectors!(a, b, insertion_point, u_vector, v_vector);
        }
        (EntityType::Wipeout(a), EntityType::Wipeout(b)) => {
            vectors!(a, b, insertion_point, u_vector, v_vector);
        }
        _ => panic!("Wrong raster type"),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 34);
    for step in 0..16 {
        eprintln!("raster step {step}");
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
        assert_eq!(rows.lines().count(), 34);
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
        assert_eq!(actual.entities().count(), 34);
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
