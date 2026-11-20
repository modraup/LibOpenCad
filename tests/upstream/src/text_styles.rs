// SPDX-License-Identifier: MPL-2.0
use acadrust::tables::TextStyle;
use acadrust::xdata::{ExtendedDataRecord, XDataValue as V};
use acadrust::{CadDocument, DxfReader, DxfWriter};
fn definition(name: &str) -> TextStyle {
    let mut s = TextStyle::new(name);
    s.is_vertical = true;
    s.flags.backward = true;
    s.flags.upside_down = true;
    s.height = 3.;
    s.width_factor = 0.75;
    s.oblique_angle = 0.25;
    s.last_height = 4.;
    s.font_file = "simplex.shx".into();
    s.big_font_file = "中文.shx".into();
    s.true_type_font = " Noto Sans CJK SC ".into();
    s.true_type_font_flags = 0x1000022;
    s.annotative = true;
    s
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for name in ["中文样式", "Neighbor"] {
        d.text_styles.add(definition(name))?;
    }
    d.assign_table_entry_handles();
    for name in ["中文样式", "Neighbor"] {
        let h = d.text_styles.get(name).unwrap().handle;
        d.set_object_xdata_record(
            h,
            ExtendedDataRecord {
                application_name: "ACAD".into(),
                values: vec![
                    V::String("source font".into()),
                    V::Integer32(34),
                    V::Real(9.5),
                    V::String("unmodeled".into()),
                ],
            },
        );
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let before = DxfReader::from_file(input)?.read()?;
    for stage in ["created", "edited"] {
        let d = DxfReader::from_file(format!("{prefix}-{stage}.dxf"))?.read()?;
        for name in ["中文样式", "Neighbor"] {
            let a = d.text_styles.get(name).unwrap();
            let mut expected = before.text_styles.get(name).unwrap().clone();
            expected.handle = a.handle;
            if stage == "edited" && name == "中文样式" {
                expected.true_type_font.clear();
                expected.true_type_font_flags = 34;
                expected.annotative = false;
                expected.last_height = 2.5;
                expected.oblique_angle = -0.5;
                expected.height = 5.;
            }
            assert_eq!(a, &expected);
            if stage == "edited" {
                let acad = d
                    .object_xdata(a.handle)
                    .unwrap()
                    .get_record("ACAD")
                    .unwrap();
                assert_eq!(
                    &acad.values[2..],
                    &[V::Real(9.5), V::String("unmodeled".into())]
                );
            }
        }
    }
    println!("Rust verified named STYLE font, geometry, generation flags, annotative XDATA, clear operations and unmodeled values");
    Ok(())
}
