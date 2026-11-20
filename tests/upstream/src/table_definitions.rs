// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{GridFlags, ViewportRenderMode};
use acadrust::tables::{Ucs, VPort, View};
use acadrust::types::Color;
use acadrust::{CadDocument, DxfReader, DxfWriter, Vector2, Vector3};
fn ucs(name: &str) -> Ucs {
    let mut u = Ucs::new(name);
    u.origin = Vector3::new(1., 2., 3.);
    u.x_axis = Vector3::new(0., 1., 0.);
    u.y_axis = Vector3::new(-1., 0., 0.);
    u.elevation = 4.;
    u.ortho_type = 2;
    u.ortho_view_type = 3;
    u.xref_reference = true;
    u
}
fn view(name: &str) -> View {
    let mut v = View::new(name);
    v.paper_space = true;
    v.perspective = true;
    v.front_clipping = true;
    v.back_clipping = true;
    v.front_clip_at_eye = true;
    v.center = Vector3::new(2., 3., 0.);
    v.direction = Vector3::new(1., 2., 3.);
    v.target = Vector3::new(4., 5., 6.);
    v.height = 17.;
    v.width = 23.;
    v.lens_length = 35.;
    v.front_clip = 2.;
    v.back_clip = 9.;
    v.twist_angle = 0.5;
    v.ucs_associated = true;
    v.ucs_origin = Vector3::new(7., 8., 9.);
    v.ucs_x_axis = Vector3::new(0., 1., 0.);
    v.ucs_y_axis = Vector3::new(-1., 0., 0.);
    v.ucs_elevation = 3.;
    v.ucs_ortho_type = 2;
    v.render_mode = ViewportRenderMode::from_value(3);
    v.camera_plottable = true;
    v.use_default_lights = false;
    v.default_lighting_type = 2;
    v.brightness = 0.2;
    v.contrast = 0.8;
    v.ambient_color = Color::from_rgb(10, 20, 30);
    v
}
fn vport(name: &str) -> VPort {
    let mut v = VPort::new(name);
    v.lower_left = Vector2::new(0.1, 0.2);
    v.upper_right = Vector2::new(0.8, 0.9);
    v.view_center = Vector2::new(1., 2.);
    v.snap_base = Vector2::new(3., 4.);
    v.snap_spacing = Vector2::new(5., 6.);
    v.grid_spacing = Vector2::new(7., 8.);
    v.view_direction = Vector3::new(1., 2., 3.);
    v.view_target = Vector3::new(4., 5., 6.);
    v.view_height = 17.;
    v.aspect_ratio = 2.;
    v.lens_length = 35.;
    v.front_clip = 2.;
    v.back_clip = 9.;
    v.snap_rotation = 0.25;
    v.view_twist = 0.5;
    v.perspective = true;
    v.front_clipping = true;
    v.back_clipping = true;
    v.ucsfollow = true;
    v.front_clip_at_eye = true;
    v.circle_zoom = 80;
    v.fast_zoom = false;
    v.ucsicon_origin = false;
    v.snap_on = true;
    v.grid_on = true;
    v.snap_style = true;
    v.snap_isopair = 2;
    v.grid_flags = GridFlags::from_bits(7);
    v.grid_major = 9;
    v.ucs_per_viewport = false;
    v.ucs_origin = Vector3::new(7., 8., 9.);
    v.ucs_x_axis = Vector3::new(0., 1., 0.);
    v.ucs_y_axis = Vector3::new(-1., 0., 0.);
    v.ucs_elevation = 3.;
    v.ucs_ortho_type = 2;
    v.render_mode = ViewportRenderMode::from_value(3);
    v.use_default_lights = false;
    v.default_lighting_type = 2;
    v.brightness = 0.2;
    v.contrast = 0.8;
    v.ambient_color = Color::from_rgb(10, 20, 30);
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for name in ["中文定义", "Neighbor"] {
        d.ucss.add(ucs(name))?;
        d.views.add(view(name))?;
        d.vports.add(vport(name))?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let before = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    for stage in ["created", "edited", "partial"] {
        let d = read(stage)?;
        for name in ["中文定义", "Neighbor"] {
            let mut u = before.ucss.get(name).unwrap().clone();
            let mut v = before.views.get(name).unwrap().clone();
            let mut p = before.vports.get(name).unwrap().clone();
            u.handle = d.ucss.get(name).unwrap().handle;
            v.handle = d.views.get(name).unwrap().handle;
            p.handle = d.vports.get(name).unwrap().handle;
            if stage != "created" && name == "中文定义" {
                u.origin = Vector3::new(10., 20., 30.);
                u.elevation = -4.;
                v.height = 27.;
                v.twist_angle = -0.75;
                v.perspective = false;
                v.front_clipping = false;
                v.back_clipping = false;
                v.front_clip_at_eye = false;
                v.ambient_color = Color::ByBlock;
                v.named_ucs_handle = u.handle;
                p.snap_spacing = Vector2::new(9., 10.);
                p.view_twist = -0.75;
                p.grid_flags = GridFlags::from_bits(0);
                p.ambient_color = Color::from_index(2);
                p.named_ucs_handle = u.handle;
            }
            if stage == "partial" && name == "Neighbor" {
                u.x_axis = Vector3::UNIT_X;
                p.view_direction.y = 0.;
            }
            assert_eq!(d.ucss.get(name).unwrap(), &u, "{stage} UCS");
            assert_eq!(d.views.get(name).unwrap(), &v, "{stage} VIEW");
            assert_eq!(d.vports.get(name).unwrap(), &p, "{stage} VPORT");
        }
    }
    println!("Rust verified UCS/VIEW/VPORT complete DXF fields, angles, flags, color transitions, UCS links, partial points and unchanged neighbors");
    Ok(())
}
