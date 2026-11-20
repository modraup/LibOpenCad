// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Line;
use acadrust::objects::{
    Material, MaterialColor, MaterialProceduralValue, MaterialTexture, ObjectType,
};
use acadrust::types::Color;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
fn value(name: &str) -> Material {
    let mut m = Material::new();
    m.name = name.into();
    m.description = "材质说明".into();
    m.ambient_color = MaterialColor {
        flag: 1,
        factor: 0.25,
        rgb: Some(0x123456),
    };
    m.diffuse_color = MaterialColor {
        flag: 1,
        factor: 0.75,
        rgb: Some(0xabcdef),
    };
    m.self_illumination = 2.5;
    m.reflectivity = 0.4;
    for (i, map) in [
        &mut m.diffuse_map,
        &mut m.specular_map,
        &mut m.reflection_map,
        &mut m.opacity_map,
        &mut m.bump_map,
        &mut m.refraction_map,
    ]
    .into_iter()
    .enumerate()
    {
        map.source = 1;
        map.file_name = format!("纹理{i}.png");
        map.blend_factor = 0.5;
        map.transform[12] = i as f64 + 2.;
    }
    m
}
fn find(d: &CadDocument, name: &str) -> Handle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::Material(m) = o {
                (m.name == name).then_some(m.handle)
            } else {
                None
            }
        })
        .unwrap()
}
fn mat(d: &CadDocument, h: Handle) -> &Material {
    if let ObjectType::Material(m) = &d.objects[&h] {
        m
    } else {
        panic!()
    }
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let owner = d.header.named_objects_dict_handle;
    for name in ["中文材质", "邻居材质"] {
        let mut m = value(name);
        m.handle = d.allocate_handle();
        m.owner = owner;
        let h = m.handle;
        if let ObjectType::Dictionary(dict) = d.objects.get_mut(&owner).unwrap() {
            dict.add_entry(name, h);
        }
        d.objects.insert(h, ObjectType::Material(m));
    }
    d.add_entity(EntityType::Line(Line::from_points(
        Vector3::ZERO,
        Vector3::UNIT_X,
    )))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let h = find(&src, "中文材质");
    let neighbor = find(&src, "邻居材质");
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    assert_eq!(mat(&src, h), mat(&round, h));
    let edited = read("edited")?;
    let actual = mat(&edited, h);
    assert_eq!(actual.opacity_percent, 0.3);
    assert_eq!(actual.diffuse_map.transform[12], 99.);
    assert_eq!(actual.color_bleed_scale, 0.75);
    assert_eq!(actual.reflectance_scale, 1.5);
    assert!(actual.two_sided_material && actual.advanced_data_present);
    assert_eq!(mat(&src, neighbor), mat(&edited, neighbor));
    let fresh = read("fresh")?;
    let m = mat(&fresh, find(&fresh, "程序材质"));
    let maps = [
        &m.diffuse_map,
        &m.specular_map,
        &m.reflection_map,
        &m.opacity_map,
        &m.bump_map,
        &m.refraction_map,
    ];
    let values = [
        MaterialProceduralValue::Bool(true),
        MaterialProceduralValue::Integer(-7),
        MaterialProceduralValue::Real(1.25),
        MaterialProceduralValue::Color(Color::from_true_color_value(0x223344)),
        MaterialProceduralValue::Text("纹理参数".into()),
    ];
    for (map, v) in maps.iter().zip(values) {
        assert_eq!(map.texture.as_ref().unwrap().procedural.as_ref(), Some(&v));
    }
    let t = maps[5].texture.as_ref().unwrap();
    if let Some(MaterialProceduralValue::Table(children)) = &t.procedural {
        assert_eq!(children.len(), 2);
        assert_eq!(children[0].0, "棋盘");
        assert_eq!(children[0].1.mode, 0);
        assert_eq!(children[1].1.mode, 1);
        assert_eq!(children[0].1.color1.rgb, Some(0xabcdef));
    } else {
        panic!("missing nested texture")
    }
    let _type_check: Option<MaterialTexture> = None;
    let normal = mat(&edited, find(&edited, "中文材质"));
    assert_eq!(normal.normal_map.file_name, "法线.png");
    assert_eq!(normal.normal_map_method, 1);
    assert_eq!(normal.normal_map_strength, 0.7);
    let assets = read("assets")?;
    let checker = mat(&assets, find(&assets, "棋盘资产"));
    assert_eq!(
        checker.diffuse_map.texture.as_ref().unwrap().color1.rgb,
        Some(0x654321)
    );
    println!("Rust verified material colors, six maps/matrices, advanced XRECORD and recursive procedural texture types");
    Ok(())
}
