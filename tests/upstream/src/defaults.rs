// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfWriter};
use std::fmt::Write;

// Freeze the default record values of initialize_defaults + the DXF section writers.
// C++ compiles this data into the library; generation is a maintainer/test operation.
pub fn generate(path: &str) -> acadrust::Result<()> {
    let doc = CadDocument::new();
    let bytes = DxfWriter::new(&doc).write_to_vec()?;
    let text = String::from_utf8(bytes).unwrap();
    let mut lines = text.lines();
    let mut header_variable = String::new();
    let mut output = String::from(
        "// SPDX-License-Identifier: MPL-2.0\n// acadrust 52034a673ff59bf6936ebf01ec794b07a97df246, CadDocument::initialize_defaults\n// and io/dxf/writer section defaults. Regenerate with generate-defaults.\nconstexpr struct { int code; const char* value; } default_pairs[] = {\n");
    while let Some(code) = lines.next() {
        let code: u16 = code.trim().parse().unwrap();
        let mut value = lines.next().unwrap().to_string();
        // Writer reserves extra save-time handles. A new C++ document starts at
        // CadDocument::new's next_handle, above all initialized identities.
        if header_variable == "$HANDSEED" && code == 5 {
            value = format!("{:X}", doc.next_handle());
        }
        header_variable = if code == 9 {
            value.clone()
        } else {
            String::new()
        };
        assert!(value.is_ascii());
        writeln!(output, "    {{{code}, {value:?}}},").unwrap();
    }
    output.push_str("};\n");
    std::fs::write(path, output)?;
    Ok(())
}

pub fn verify(format: &str, prefix: &str) -> acadrust::Result<()> {
    use acadrust::{DxfReader, DxfVersion};
    use std::io::Cursor;
    let regenerated = format!("{prefix}-defaults.inc");
    generate(&regenerated)?;
    let source =
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../src/dxf_defaults.inc");
    assert_eq!(
        std::fs::read_to_string(source)?,
        std::fs::read_to_string(regenerated)?
    );
    let units = if format == "binary" { 1 } else { 4 };
    let mut defaults = CadDocument::new();
    defaults.header.insertion_units = units;
    let mut writer = DxfWriter::new(&defaults);
    writer.set_binary(format == "binary");
    let baseline = writer.write_to_vec()?;
    let mut expected = DxfReader::from_reader(Cursor::new(baseline))?.read()?;
    let actual = DxfReader::from_file(format!("{prefix}.dxf"))?.read()?;
    assert_eq!(actual.version, DxfVersion::AC1032);
    assert_eq!(actual.header.insertion_units, units);
    assert_eq!(actual.entities().count(), 0);
    assert_eq!(actual.model_space_entities().count(), 0);
    assert_eq!(actual.header.handle_seed, defaults.next_handle());
    // The Rust writer advances its emitted seed one extra step for save-time work.
    expected.header.handle_seed = actual.header.handle_seed;
    assert_eq!(actual.header, expected.header);
    assert_eq!(actual.layers, expected.layers);
    assert_eq!(actual.line_types, expected.line_types);
    assert_eq!(actual.text_styles, expected.text_styles);
    assert_eq!(actual.dim_styles, expected.dim_styles);
    assert_eq!(actual.app_ids, expected.app_ids);
    assert_eq!(actual.vports, expected.vports);
    assert_eq!(actual.views, expected.views);
    assert_eq!(actual.ucss, expected.ucss);
    assert_eq!(actual.block_records, expected.block_records);
    assert_eq!(actual.objects, expected.objects);
    assert_eq!(actual.classes, expected.classes);
    assert_eq!(actual.audit(), expected.audit());
    let added = DxfReader::from_file(format!("{prefix}-added.dxf"))?.read()?;
    assert_eq!(added.layers.len(), actual.layers.len() + 1);
    assert_eq!(added.line_types.len(), actual.line_types.len() + 1);
    assert_eq!(added.layers.get("新增图层").unwrap().handle.value(), 0x33);
    assert_eq!(added.layers.get("新增图层").unwrap().line_type, "新增虚线");
    assert_eq!(
        added.line_types.get("新增虚线").unwrap().handle.value(),
        0x32
    );
    assert_eq!(
        added.line_types.get("新增虚线").unwrap().pattern_length,
        0.75
    );
    assert_eq!(added.header.handle_seed, 0x34);
    assert_eq!(added.objects, actual.objects);
    assert_eq!(added.block_records, actual.block_records);
    Ok(())
}
