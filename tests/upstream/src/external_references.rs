// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::raster_image::ClipType;
use acadrust::entities::underlay::{UnderlayDefinition, UnderlayDisplayFlags, UnderlayType};
use acadrust::entities::wipeout::{WipeoutClipMode, WipeoutClipType};
use acadrust::entities::{RasterImage, Underlay, Wipeout};
use acadrust::objects::{ImageDefinition, ImageDefinitionReactor, ObjectType, RasterVariables};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
fn put(d: &mut CadDocument, name: &str, handle: Handle, o: ObjectType) {
    let root = d.header.named_objects_dict_handle;
    if let ObjectType::Dictionary(dict) = d.objects.get_mut(&root).unwrap() {
        dict.add_entry(name, handle);
    }
    d.objects.insert(handle, o);
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let root = d.header.named_objects_dict_handle;
    let mut def = ImageDefinition::with_dimensions("图像.png", 64, 32);
    def.handle = d.allocate_handle();
    def.owner = root;
    let dh = def.handle;
    put(&mut d, "图片定义", dh, ObjectType::ImageDefinition(def));
    for paper in [false, true] {
        let mut img = RasterImage::new("图像.png", Vector3::new(1., 2., 3.), 64., 32.);
        img.definition_handle = Some(dh);
        img.clip_boundary.clip_type = ClipType::Polygonal;
        img.clip_boundary.vertices = vec![
            Vector2::new(0., 0.),
            Vector2::new(30., 0.),
            Vector2::new(10., 20.),
        ];
        img.clipping_enabled = true;
        let h = if paper {
            d.add_paper_space_entity(EntityType::RasterImage(img))?
        } else {
            d.add_entity(EntityType::RasterImage(img))?
        };
        let mut r = ImageDefinitionReactor::new(h);
        r.handle = d.allocate_handle();
        r.owner = h;
        let rh = r.handle;
        d.objects.insert(rh, ObjectType::ImageDefinitionReactor(r));
        if let Some(EntityType::RasterImage(i)) = d.get_entity_mut(h) {
            i.definition_reactor_handle = Some(rh);
        }
    }
    let mut w = Wipeout::new();
    w.clip_type = WipeoutClipType::Polygonal;
    w.clip_mode = WipeoutClipMode::Inside;
    w.clip_boundary_vertices = vec![
        Vector2::new(0., 0.),
        Vector2::new(4., 0.),
        Vector2::new(2., 3.),
    ];
    d.add_entity(EntityType::Wipeout(w))?;
    for (i, kind) in [UnderlayType::Pdf, UnderlayType::Dwf, UnderlayType::Dgn]
        .into_iter()
        .enumerate()
    {
        let mut def = UnderlayDefinition::new(kind);
        def.handle = d.allocate_handle();
        def.owner_handle = root;
        def.file_path = format!("底图{i}.dat");
        def.page_name = "页面一".into();
        def.unloaded = true;
        let dh = def.handle;
        put(
            &mut d,
            &format!("底图{i}"),
            dh,
            ObjectType::UnderlayDefinition(def),
        );
        let mut u = Underlay::new(kind);
        u.definition_handle = dh;
        u.rotation = 0.5;
        u.flags = UnderlayDisplayFlags::from_bits_truncate(19);
        u.clip_inverted = true;
        u.clip_boundary_vertices = vec![Vector2::new(-1., -2.), Vector2::new(5., 6.)];
        d.add_entity(EntityType::Underlay(u))?;
    }
    let mut vars = RasterVariables::new();
    vars.handle = d.allocate_handle();
    vars.owner = root;
    put(
        &mut d,
        "ACAD_IMAGE_VARS",
        vars.handle,
        ObjectType::RasterVariables(vars),
    );
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn image(d: &CadDocument, h: Handle) -> &RasterImage {
    if let EntityType::RasterImage(i) = d.get_entity(h).unwrap() {
        i
    } else {
        panic!()
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let h = src
        .entities_in_block("*Model_Space")
        .find(|e| matches!(e, EntityType::RasterImage(_)))
        .unwrap()
        .common()
        .handle;
    let neighbor = src
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let dh = image(&src, h).definition_handle.unwrap();
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for e in src.entities() {
        assert_eq!(round.get_entity(e.common().handle), Some(e));
    }
    let edited = read("edited")?;
    let mut expected = image(&src, h).clone();
    expected.insertion_point.z = 12.;
    expected.clip_boundary.vertices[1].x = 42.;
    expected.brightness = 75;
    assert_eq!(image(&edited, h), &expected);
    assert_eq!(edited.get_entity(neighbor), src.get_entity(neighbor));
    for e in edited.entities() {
        if let EntityType::Underlay(u) = e {
            assert_eq!(u.x_scale, 2.);
            assert_eq!(u.clip_boundary_vertices[0].y, -7.);
            assert!(u.clip_inverted);
        }
    }
    let copied = read("copied")?;
    let copy = copied
        .entities()
        .find(|e| edited.get_entity(e.common().handle).is_none())
        .unwrap()
        .common()
        .handle;
    let r = image(&copied, copy).definition_reactor_handle.unwrap();
    assert_ne!(r, image(&src, h).definition_reactor_handle.unwrap());
    if let ObjectType::ImageDefinitionReactor(reactor) = &copied.objects[&r] {
        assert_eq!(reactor.owner, copy);
        assert_eq!(reactor.image_handle, copy);
    } else {
        panic!()
    }
    if let ObjectType::ImageDefinition(def) = &copied.objects[&dh] {
        assert!(def.reactors.contains(&r));
    } else {
        panic!()
    }
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(copy).is_none() && !deleted.objects.contains_key(&r));
    assert_eq!(image(&deleted, h), &expected);
    let fresh = read("fresh")?;
    assert!(!fresh
        .objects
        .values()
        .any(|o| matches!(o, ObjectType::Unknown { .. })));
    assert_eq!(
        fresh
            .entities()
            .filter(|e| matches!(e, EntityType::RasterImage(_)))
            .count(),
        2
    );
    for e in fresh.entities() {
        if let EntityType::RasterImage(i) = e {
            assert_eq!(i.size, Vector2::new(64., 32.));
            assert!(i.definition_reactor_handle.is_some());
            if let ObjectType::ImageDefinitionReactor(r) =
                &fresh.objects[&i.definition_reactor_handle.unwrap()]
            {
                assert_eq!(r.image_handle, i.common.handle);
                assert_eq!(r.owner, i.common.handle);
            } else {
                panic!()
            }
        }
        if let EntityType::Underlay(u) = e {
            assert_eq!(u.rotation, 0.5);
            assert_eq!(u.clip_boundary_vertices.len(), 2);
        }
    }
    let removed = read("removed")?;
    assert!(removed.entities().next().is_none());
    assert!(removed.objects.contains_key(&dh));
    let underlay_copies = read("underlay-copied")?;
    let underlay_deleted = read("underlay-deleted")?;
    for e in fresh.entities() {
        if let EntityType::Underlay(u) = e {
            for (doc, count) in [(&fresh, 1), (&underlay_copies, 2), (&underlay_deleted, 1)] {
                if let ObjectType::UnderlayDefinition(def) = &doc.objects[&u.definition_handle] {
                    assert_eq!(def.reactors.len(), count);
                    for handle in &def.reactors {
                        assert!(matches!(
                            doc.get_entity(*handle),
                            Some(EntityType::Underlay(_))
                        ));
                    }
                } else {
                    panic!()
                }
            }
        }
    }
    println!("Rust verified images, wipeout closure, all underlays, definition ownership, reactor copy/delete and shared definition retention");
    Ok(())
}
