// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Circle, Viewport};
use acadrust::objects::{Layout, ObjectType};
use acadrust::types::Color;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn layout_mut<'a>(d: &'a mut CadDocument, name: &str) -> &'a mut Layout {
    d.objects
        .values_mut()
        .find_map(|o| {
            if let ObjectType::Layout(l) = o {
                (l.name == name).then_some(l)
            } else {
                None
            }
        })
        .unwrap()
}
fn layout<'a>(d: &'a CadDocument, name: &str) -> &'a Layout {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::Layout(l) = o {
                (l.name == name).then_some(l)
            } else {
                None
            }
        })
        .unwrap()
}
fn configure(l: &mut Layout) {
    l.min_limits = (-1., -2.);
    l.max_limits = (300., 220.);
    l.insertion_base = (1., 2., 3.);
    l.min_extents = (-3., -4., -5.);
    l.max_extents = (400., 300., 6.);
    l.elevation = 7.;
    l.ucs_origin = (8., 9., 10.);
    l.ucs_ortho_type = 1;
    l.paper_size = "ISO_A4_(210.00_x_297.00_MM)".into();
    l.paper_width = 210.;
    l.paper_height = 297.;
    l.plot_paper_units = 1;
    l.plot_rotation = 1;
    l.plot_margin_left = 3.;
    l.plot_margin_bottom = 4.;
    l.plot_margin_right = 5.;
    l.plot_margin_top = 6.;
    l.plot_scale_numerator = 1.;
    l.plot_scale_denominator = 2.;
    l.plot_scale_factor = 0.5;
    l.plot_style_sheet = "monochrome.ctb".into();
    l.shade_plot_dpi = 600;
}
pub(crate) fn viewport() -> Viewport {
    let mut v = Viewport::new();
    v.center = Vector3::new(100., 120., 0.);
    v.width = 180.;
    v.height = 200.;
    v.view_center = Vector3::new(1., 2., 0.);
    v.view_direction = Vector3::new(1., 1., 1.);
    v.view_target = Vector3::new(3., 4., 5.);
    v.view_height = 25.;
    v.snap_base = Vector3::new(2., 3., 0.);
    v.snap_spacing = Vector3::new(4., 5., 0.);
    v.grid_spacing = Vector3::new(6., 7., 0.);
    v.snap_angle = 0.25;
    v.twist_angle = -0.5;
    v.status.locked = true;
    v.grid_flags.adaptive = true;
    v.grid_flags.subdivision = true;
    v.ucs_per_viewport = true;
    v.ucs_at_origin = true;
    v.ucs_origin = Vector3::new(7., 8., 9.);
    v.elevation = 3.;
    v.shade_plot_mode = 2;
    v.grid_major = 10;
    v.default_lighting = false;
    v.default_lighting_type = 2;
    v.brightness = 0.3;
    v.contrast = 0.7;
    v.ambient_color = Color::from_rgb(12, 34, 56);
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for name in ["图纸二", "Sheet3"] {
        d.add_layout(name)?;
        configure(layout_mut(&mut d, name));
        let circle = d.add_entity_to_layout(
            EntityType::Circle(Circle::from_center_radius(
                Vector3::new(100., 120., 0.),
                80.,
            )),
            name,
        )?;
        let mut v = viewport();
        v.clip_boundary_handle = circle;
        v.status.non_rectangular_clipping = true;
        v.frozen_layers = vec![d.layers.get("0").unwrap().handle];
        d.add_entity_to_layout(EntityType::Viewport(Box::new(v)), name)?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn compare_layout(a: &Layout, b: &Layout) {
    let mut b = b.clone();
    b.handle = a.handle;
    b.owner = a.owner;
    b.block_record = a.block_record;
    b.viewport = a.viewport;
    b.viewports = a.viewports.clone();
    b.raw_plot_settings_codes = a.raw_plot_settings_codes.clone();
    assert_eq!(a, &b);
}
fn geometry(a: &EntityType, b: &EntityType) {
    let mut b = b.clone();
    *b.common_mut() = a.common().clone();
    if let (EntityType::Viewport(a), EntityType::Viewport(b)) = (a, &mut b) {
        b.clip_boundary_handle = a.clip_boundary_handle;
        b.frozen_layers = a.frozen_layers.clone();
    }
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let input = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    assert_eq!(layout(&read("gap")?, "After gap").tab_order, 43);
    let made = read("created")?;
    for name in ["图纸二", "Sheet3"] {
        compare_layout(layout(&made, name), layout(&input, name));
        let block = made
            .block_records
            .iter()
            .find(|b| b.handle == layout(&made, name).block_record)
            .unwrap();
        let expected = input
            .block_records
            .iter()
            .find(|b| b.handle == layout(&input, name).block_record)
            .unwrap();
        let a: Vec<_> = made.entities_in_block(&block.name).collect();
        let b: Vec<_> = input.entities_in_block(&expected.name).collect();
        assert_eq!(a.len(), 3);
        assert_eq!(b.len(), 3);
        for (a, b) in a.into_iter().zip(b) {
            geometry(a, b);
        }
    }
    let after = read("edited")?;
    let before = read("before")?;
    let first = layout(&before, "图纸二");
    let mut expected = first.clone();
    expected.paper_width = 420.;
    expected.paper_height = 297.;
    expected.plot_rotation = 0;
    expected.min_limits = (11., 12.);
    expected.elevation = 13.;
    compare_layout(layout(&after, "图纸二"), &expected);
    compare_layout(layout(&after, "Sheet3"), layout(&before, "Sheet3"));
    let block = before
        .block_records
        .iter()
        .find(|b| b.handle == first.block_record)
        .unwrap();
    let target = before
        .entities_in_block(&block.name)
        .find(|e| matches!(e,EntityType::Viewport(v) if v.id==2))
        .unwrap()
        .common()
        .handle;
    for e in before.entities() {
        let mut expected = e.clone();
        if e.common().handle == target {
            let EntityType::Viewport(v) = &mut expected else {
                panic!()
            };
            v.view_height = 50.;
            v.twist_angle = 0.75;
            v.status.is_on = false;
            v.dxf_status = Some(0);
            v.frozen_layers.clear();
            v.ambient_color = Color::from_index(3);
        }
        geometry(after.get_entity(e.common().handle).unwrap(), &expected);
    }
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(target).is_none());
    assert_eq!(deleted.entities().count(), before.entities().count() - 1);
    compare_layout(layout(&deleted, "Sheet3"), layout(&before, "Sheet3"));
    println!("Rust verified multi-layout linkage, plot settings, viewport fields, layers, clipping and independent edits");
    Ok(())
}
