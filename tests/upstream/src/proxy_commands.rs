// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::proxy_graphics::*;
use acadrust::Vector3;
pub fn records() -> Vec<ProxyGraphicRecord> {
    use ProxyGraphicCommand as C;
    let a = Vector3::new(1., 2., 3.);
    let b = Vector3::new(4., 5., 6.);
    let c = Vector3::new(7., 8., 9.);
    let mut commands = vec![
        C::Extents([a, b]),
        C::Circle {
            center: a,
            radius: 2.,
            normal: b,
        },
        C::Circle3([a, b, c]),
        C::CircularArc {
            center: a,
            radius: 4.,
            normal: b,
            start_direction: c,
            sweep_angle: 0.75,
            arc_type: -1,
        },
        C::CircularArc3 {
            points: [a, b, c],
            arc_type: 2,
        },
        C::Polyline(vec![a, b]),
        C::Polygon(vec![a, b, c]),
        C::PolylineWithNormal {
            vertices: vec![a, b, c],
            normal: c,
        },
        C::PushTransform([1.; 16]),
        C::PopTransform,
        C::ColorIndex(256),
        C::LayerIndex(3),
        C::LineTypeIndex(4),
        C::SelectionMarker(0xfffffff1),
        C::Fill(true),
        C::Fill(false),
        C::EncodedColor(0xc2123456),
        C::LineWeight(-3),
        C::LineTypeScale(0.5),
        C::Thickness(-2.),
        C::Compatibility51Zero,
    ];
    let mut t = ProxyLegacyText {
        position: a,
        normal: b,
        direction: c,
        text: vec![0xd6, 0xd0, 0xce, 0xc4],
        height: 2.,
        width_factor: 0.5,
        oblique_angle: 0.25,
        styling: None,
    };
    commands.push(C::LegacyText(Box::new(t.clone())));
    t.styling = Some(ProxyLegacyTextStyle {
        declared_length: -1,
        processing_mode: 1,
        tracking: 1.25,
        text_flags: [true, false, true, false, true],
        font_file: b"simplex.shx".to_vec(),
        big_font_file: vec![0xd6, 0xd0],
    });
    commands.push(C::LegacyText(Box::new(t)));
    commands.push(C::StyledUnicodeText(Box::new(ProxyStyledUnicodeText {
        position: a,
        normal: b,
        direction: c,
        text: "中文😀".into(),
        declared_length: 4,
        processing_mode: 0,
        height: 3.,
        width_factor: 0.75,
        oblique_angle: 0.5,
        tracking: 1.5,
        text_flags: [false, true, false, true, false],
        bold: true,
        italic: false,
        charset: 134,
        pitch_family: 1,
        family: "中文字体".into(),
        font_file: "字体.ttf".into(),
        big_font_file: String::new(),
    })));
    let checked = CheckedProxyGraphics {
        records: commands
            .into_iter()
            .map(CheckedProxyGraphicRecord::Command)
            .collect(),
    };
    ProxyGraphics::decode(&checked.encode_checked().unwrap())
        .unwrap()
        .records
}
