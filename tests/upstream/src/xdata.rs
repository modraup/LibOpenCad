// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::mtext::MTextColumnData;
use acadrust::entities::{Line, MText};
use acadrust::tables::AppId;
use acadrust::types::Handle;
use acadrust::xdata::{ExtendedDataRecord, XDataValue as V};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn payload(h: Handle) -> ExtendedDataRecord {
    ExtendedDataRecord {
        application_name: "中文应用".into(),
        values: vec![
            V::String("中文数据".into()),
            V::ControlString("{".into()),
            V::LayerName("0".into()),
            V::BinaryData(vec![0, 1, 127, 128, 255]),
            V::Handle(h),
            V::Point3D(Vector3::new(1., 2., 3.)),
            V::Position3D(Vector3::new(4., 5., 6.)),
            V::Displacement3D(Vector3::new(-1., -2., -3.)),
            V::Direction3D(Vector3::new(0., 0., 1.)),
            V::Real(1.25),
            V::Distance(2.5),
            V::ScaleFactor(0.5),
            V::Integer16(-32768),
            V::Integer32(-2147483647),
            V::ControlString("}".into()),
        ],
    }
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.app_ids.add(AppId::new("中文应用"))?;
    let mut t = MText::new();
    t.value = "双栏文字".into();
    t.column_data = MTextColumnData {
        column_type: 2,
        column_count: 2,
        width: 8.,
        gutter: 2.,
        heights: vec![5., 6.],
        ..Default::default()
    };
    let h = d.add_entity(EntityType::MText(Box::new(t)))?;
    d.get_entity_mut(h)
        .unwrap()
        .common_mut()
        .extended_data
        .add_record(payload(h));
    let h = d.add_entity(EntityType::Line(Line::from_points(
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 5., 6.),
    )))?;
    d.get_entity_mut(h)
        .unwrap()
        .common_mut()
        .extended_data
        .add_record(payload(h));
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let target = original
        .entities()
        .find(|e| matches!(e, EntityType::MText(_)))
        .unwrap()
        .common()
        .handle;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    for stage in ["edited", "copied", "removed", "deleted"] {
        let d = read(stage)?;
        assert!(d.app_ids.get("Extra").is_some());
        for e in d.entities() {
            let h = e.common().handle;
            let copied = original.get_entity(h).is_none();
            let data = &e.common().extended_data;
            if copied && stage == "removed" {
                assert!(data.get_record("中文应用").is_none());
            } else {
                let mut expected = payload(h);
                if h == target || copied {
                    expected.values[0] = V::String("修改后的中文".into());
                }
                assert_eq!(data.get_record("中文应用").unwrap(), &expected);
            }
            if h == target || copied {
                assert_eq!(
                    data.get_record("Extra").unwrap().values,
                    vec![V::Integer32(i32::MAX)]
                );
            } else {
                assert_eq!(e, original.get_entity(h).unwrap());
            }
        }
        assert_eq!(
            d.entities().count(),
            if stage == "copied" || stage == "removed" {
                3
            } else {
                2
            }
        );
    }
    for stage in ["created", "converted"] {
        let d = read(stage)?;
        let e = d.entities().next().unwrap();
        assert_eq!(
            e.common().extended_data.get_record("中文应用").unwrap(),
            &payload(e.common().handle)
        );
    }
    println!("Rust verified every XDATA value type, APPID, independent app edits, embedded MTEXT copy references and removal");
    Ok(())
}
