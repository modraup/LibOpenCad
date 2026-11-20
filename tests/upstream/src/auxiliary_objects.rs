// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Line;
use acadrust::objects::{
    BookColor, DictionaryVariable, Group, ObjectType, Scale, SortEntitiesTable, SpatialFilter,
    WipeoutVariables,
};
use acadrust::types::Color;
use acadrust::types::Matrix4;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
fn insert(d: &mut CadDocument, name: &str, mut o: ObjectType) {
    let root = d.header.named_objects_dict_handle;
    let h = d.allocate_handle();
    match &mut o {
        ObjectType::Group(v) => {
            v.handle = h;
            v.owner = root;
        }
        ObjectType::Scale(v) => {
            v.handle = h;
            v.owner_handle = root;
        }
        ObjectType::SpatialFilter(v) => {
            v.handle = h;
            v.owner = root;
        }
        ObjectType::SortEntitiesTable(v) => {
            v.handle = h;
            v.owner_handle = root;
        }
        ObjectType::DictionaryVariable(v) => {
            v.handle = h;
            v.owner_handle = root;
        }
        ObjectType::BookColor(v) => {
            v.handle = h;
            v.owner = root;
        }
        ObjectType::WipeoutVariables(v) => {
            v.handle = h;
            v.owner = root;
        }
        _ => panic!(),
    }
    if let ObjectType::Dictionary(dict) = d.objects.get_mut(&root).unwrap() {
        dict.add_entry(name, h);
    }
    d.objects.insert(h, o);
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let a = d.add_entity(EntityType::Line(Line::from_points(
        Vector3::ZERO,
        Vector3::UNIT_X,
    )))?;
    let b = d.add_entity(EntityType::Line(Line::from_points(
        Vector3::UNIT_Y,
        Vector3::UNIT_Z,
    )))?;
    let block = d.get_entity(a).unwrap().common().owner_handle;
    let mut group = Group::new("组");
    group.description = "中文分组".into();
    group.add_entity(a);
    group.add_entity(b);
    insert(&mut d, "组", ObjectType::Group(group));
    insert(
        &mut d,
        "比例",
        ObjectType::Scale(Scale::new("1:50", 1., 50.)),
    );
    let mut sort = SortEntitiesTable::for_block(block);
    sort.add_entry(a, Handle::new(20));
    sort.add_entry(b, Handle::new(10));
    insert(&mut d, "排序", ObjectType::SortEntitiesTable(sort));
    for name in ["裁剪", "邻居裁剪"] {
        let mut clip = SpatialFilter::new();
        clip.boundary_points = vec![Vector2::new(1., 2.), Vector2::new(10., 20.)];
        clip.front_clip = Some(12.);
        clip.back_clip = Some(-7.);
        clip.inverse_block_transform = Matrix4::translation(1., 2., 3.);
        clip.clip_bound_transform = Matrix4::scaling(2., 3., 4.);
        insert(&mut d, name, ObjectType::SpatialFilter(clip));
    }
    insert(
        &mut d,
        "变量",
        ObjectType::DictionaryVariable(DictionaryVariable::new("变量", "原值")),
    );
    let mut color = BookColor::new();
    color.color = Color::from_true_color_value(0x123456);
    color.book_name = "色本".into();
    color.color_name = "蓝色".into();
    insert(&mut d, "颜色", ObjectType::BookColor(color));
    insert(
        &mut d,
        "遮罩变量",
        ObjectType::WipeoutVariables(WipeoutVariables::new()),
    );
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn named(d: &CadDocument, n: &str) -> Handle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::Dictionary(d) = o {
                d.get(n)
            } else {
                None
            }
        })
        .unwrap()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for name in ["组", "比例", "裁剪", "邻居裁剪", "变量", "颜色", "遮罩变量"] {
        let h = named(&src, name);
        assert_eq!(round.objects[&h], src.objects[&h]);
    }
    let edited = read("edited")?;
    if let ObjectType::SpatialFilter(v) = &edited.objects[&named(&edited, "裁剪")] {
        assert_eq!(v.front_clip, Some(22.));
        assert_eq!(v.clip_bound_transform.m[1][3], 44.);
        assert_eq!(v.boundary_points[0].x, -3.);
    } else {
        panic!()
    }
    assert_eq!(
        edited.objects[&named(&edited, "邻居裁剪")],
        src.objects[&named(&src, "邻居裁剪")]
    );
    if let ObjectType::Group(v) = &edited.objects[&named(&edited, "组")] {
        assert_eq!(v.entities.len(), 1);
        assert!(!v.selectable);
    }
    if let ObjectType::Scale(v) = &edited.objects[&named(&edited, "比例")] {
        assert_eq!(v.drawing_units, 100.);
    }
    let fresh = read("fresh")?;
    if let ObjectType::SpatialFilter(v) = &fresh.objects[&named(&fresh, "新裁剪")] {
        assert_eq!(v.front_clip, Some(5.));
        assert_eq!(v.inverse_block_transform.m[0][3], 8.);
    } else {
        panic!()
    }
    if let ObjectType::SortEntitiesTable(v) = &fresh.objects[&named(&fresh, "新排序")] {
        let entries: Vec<_> = v.entries().collect();
        assert_eq!(entries.len(), 2);
        assert!(entries[0].sort_handle.value() > entries[1].sort_handle.value());
    }
    let color_order = read("color-order")?;
    if let ObjectType::BookColor(v) = &color_order.objects[&named(&color_order, "新颜色")] {
        assert_eq!(v.color, Color::from_true_color_value(0x123456));
        assert_eq!(v.color_name, "最终颜色");
        assert_eq!(v.book_name, "前色本");
    } else {
        panic!()
    }
    println!("Rust verified auxiliary definitions, sort ordering, paired clip transforms, group references and independent neighbors");
    Ok(())
}
