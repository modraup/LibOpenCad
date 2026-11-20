// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::hatch::{BoundaryEdge, BoundaryPath, BoundaryPathFlags, PolylineEdge};
use acadrust::entities::{
    Hatch, Light, LightPhotometricData, Ole2Frame, SectionSymbol, SectionSymbolPoint, ViewBorder,
};
use acadrust::types::Color;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector2, Vector3};
pub(crate) fn entities() -> Vec<EntityType> {
    let mut hatch = Hatch::new();
    hatch.is_mpolygon = true;
    hatch.mpolygon_hatch_color = Color::from_index(3);
    hatch.mpolygon_x_direction = Vector2::new(2., 3.);
    for size in [10., 2.] {
        let mut path = BoundaryPath::with_flags(BoundaryPathFlags::from_bits(3));
        path.edges.push(BoundaryEdge::Polyline(PolylineEdge {
            vertices: vec![
                Vector3::ZERO,
                Vector3::new(size, 0., 0.5),
                Vector3::new(size, size, 0.),
            ],
            is_closed: true,
        }));
        hatch.paths.push(path);
    }
    let mut light = Light::new();
    light.name = "光源".into();
    light.light_type = 3;
    light.light_color = Color::from_rgb(13, 24, 35);
    light.position = Vector3::new(1., 2., 3.);
    light.target = Vector3::new(4., 5., 6.);
    light.intensity = 2.;
    light.attenuation_start_limit = 3.;
    light.attenuation_end_limit = 40.;
    light.hotspot_angle = 0.2;
    light.falloff_angle = 0.6;
    light.shadow_map_size = 512;
    light.shadow_map_softness = 7;
    light.photometric_mode = true;
    let mut photo = LightPhotometricData::default();
    photo.web_file = "灯光.ies".into();
    photo.has_web_file = true;
    photo.physical_intensity = 120.;
    photo.web_rotation = Vector3::new(1., 0., 3.);
    photo.web_angles = [1., 2., 3., 0., 5.];
    light.photometric_data = Some(photo);
    let mut ole = Ole2Frame::new();
    ole.source_application = "工作表".into();
    ole.upper_left_corner = Vector3::new(-5., 20., 2.);
    ole.lower_right_corner = Vector3::new(10., -5., 2.);
    ole.lock_aspect = 1;
    ole.storage = acadrust::compound_file::StructuredStoragePayload::decode(&[1, 2, 3, 4, 5]);
    let mut section = SectionSymbol::new();
    section.raw_flags_90 = 7;
    section.raw_point_count_90 = 2;
    section.raw_point_record_count = 2;
    for x in [1., 10.] {
        let mut p = SectionSymbolPoint::new();
        p.point = Vector3::new(x, 2., 3.);
        p.bulge = 0.25;
        p.label = "剖面甲".into();
        p.label_offset = Vector3::new(4., 5., 6.);
        p.raw_flag_280 = 3;
        section.points.push(p);
    }
    section.sync_display_fields();
    let mut view = ViewBorder::new();
    view.min = [1., 2.];
    view.max = [10., 20.];
    view.center = [5.5, 11.];
    view.scale = 2.;
    view.rotation_angle = 0.75;
    vec![
        EntityType::Hatch(Box::new(hatch)),
        EntityType::Light(Box::new(light)),
        EntityType::Ole2Frame(Box::new(ole)),
        EntityType::SectionSymbol(section),
        EntityType::ViewBorder(view),
    ]
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for e in entities() {
        d.add_entity(e.clone())?;
        d.add_paper_space_entity(e)?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    // The pinned LIGHT writer emits an f64 for group 63 in the photometric
    // point, corrupting its binary stream. Canonicalize the Rust ASCII pairs
    // with Rust's group-code-aware binary writer; all other data stays identical.
    use acadrust::io::dxf::{DxfBinaryWriter, DxfStreamWriter};
    let text = std::fs::read_to_string(ascii)?;
    let mut lines = text.lines();
    let mut writer = DxfBinaryWriter::new(std::fs::File::create(binary)?)?;
    while let Some(code) = lines.next() {
        let code: i32 = code.trim().parse().unwrap();
        let value = lines.next().unwrap();
        let value = if code == 63 {
            let n: f64 = value.trim().parse().unwrap();
            assert_eq!(n, n.trunc());
            (n as i16).to_string()
        } else {
            value.to_string()
        };
        writer.write_string(code, &value)?;
    }
    Ok(())
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
        match e {
            EntityType::Hatch(h) => {
                assert_eq!(h.mpolygon_x_direction, Vector2::new(7., 8.));
                assert_eq!(h.paths.len(), 2);
            }
            EntityType::Light(l) => {
                assert_eq!(l.intensity, 4.);
                assert_eq!(
                    l.photometric_data.as_ref().unwrap().physical_intensity,
                    240.
                );
            }
            EntityType::Ole2Frame(o) => {
                assert_eq!(o.upper_left_corner.x, -10.);
                assert_eq!(o.encoded_payload(), vec![9, 8, 7]);
            }
            EntityType::SectionSymbol(s) => {
                assert_eq!(s.points[1].bulge, -0.5);
                assert_eq!(s.points[0].label, "修改剖面");
            }
            EntityType::ViewBorder(v) => {
                assert_eq!(v.rotation_angle, -0.25);
                assert_eq!(v.center, [9., 10.]);
            }
            _ => panic!("Unexpected entity"),
        }
    }
    let copied = read("copied")?;
    assert_eq!(copied.entities().count(), 15);
    for copy in copied
        .entities()
        .filter(|e| src.get_entity(e.common().handle).is_none())
    {
        let original = edited
            .entities_in_block("*Model_Space")
            .find(|e| std::mem::discriminant(*e) == std::mem::discriminant(copy))
            .unwrap();
        let mut expected = original.clone();
        *expected.common_mut() = copy.common().clone();
        assert_eq!(&expected, copy);
    }
    let deleted = read("deleted")?;
    for e in edited.entities() {
        assert_eq!(deleted.get_entity(e.common().handle), Some(e));
    }
    assert_eq!(deleted.entities().count(), 10);
    let fresh = read("fresh")?;
    assert_eq!(fresh.entities().count(), 5);
    println!("Rust verified MPOLYGON holes, photometric LIGHT, OLE bytes, repeated section points and view-border fields, copy/delete and neighbors");
    Ok(())
}
