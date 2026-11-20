// SPDX-License-Identifier: MPL-2.0
use acadrust::tables::{BlockRecord, DimStyle};
use acadrust::{CadDocument, DxfReader, DxfWriter};
fn definition(d: &CadDocument, name: &str) -> DimStyle {
    let mut v = DimStyle::new(name);
    v.xref_reference = true;
    v.annotative = true;
    v.dimclrd = 1;
    v.dimdle = 0.125;
    v.dimdli = 3.875;
    v.dimgap = 0.75;
    v.dimlwd = 25;
    v.dimsd1 = true;
    v.dimsd2 = true;
    v.dimclre = 1;
    v.dimexe = 1.375;
    v.dimexo = 0.75;
    v.dimlwe = 50;
    v.dimse1 = true;
    v.dimse2 = true;
    v.dimfxl = 1.125;
    v.dimfxlon = true;
    v.dimasz = 0.305;
    v.dimblk = d.block_records.get("Arrows").unwrap().handle;
    v.dimblk1 = d.block_records.get("Arrows").unwrap().handle;
    v.dimblk2 = d.block_records.get("Arrows").unwrap().handle;
    v.dimldrblk = d.block_records.get("Arrows").unwrap().handle;
    v.dimsah = true;
    v.dimcen = 0.215;
    v.dimtsz = 0.125;
    v.dimclrt = 1;
    v.dimtxt = 0.305;
    v.dimjust = 1;
    v.dimtad = 1;
    v.dimtvp = 0.125;
    v.dimtih = true;
    v.dimtoh = true;
    v.dimtix = true;
    v.dimsoxd = true;
    v.dimtfill = 2;
    v.dimtfillclr = 5;
    v.dimtmove = 1;
    v.dimtxtdirection = true;
    v.dimtxsty_handle = d.text_styles.get("Standard").unwrap().handle;
    v.dimscale = 1.125;
    v.dimlfac = 1.125;
    v.dimlunit = 1;
    v.dimdec = 1;
    v.dimrnd = 0.125;
    v.dimdsep = 44;
    v.dimzin = 1;
    v.dimaunit = 1;
    v.dimadec = 1;
    v.dimazin = 1;
    v.dimalt = true;
    v.dimaltf = 25.525;
    v.dimaltd = 1;
    v.dimaltu = 1;
    v.dimalttd = 1;
    v.dimaltrnd = 0.125;
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut block = BlockRecord::new("Arrows");
    block.handle = d.allocate_handle();
    block.block_entity_handle = d.allocate_handle();
    block.block_end_handle = d.allocate_handle();
    d.block_records.add(block)?;
    d.assign_table_entry_handles();
    for name in ["中文标注", "Neighbor"] {
        d.dim_styles.add(definition(&d, name))?;
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let before = DxfReader::from_file(input)?.read()?;
    for stage in ["created", "edited", "omitted"] {
        let d = DxfReader::from_file(format!("{prefix}-{stage}.dxf"))?.read()?;
        for name in ["中文标注", "Neighbor"] {
            let a = d.dim_styles.get(name).unwrap();
            let mut expected = before.dim_styles.get(name).unwrap().clone();
            if stage == "created" {
                expected = definition(&d, name);
            }
            if stage != "created" && name == "中文标注" {
                expected.dimtfill = 0;
                expected.dimtfillclr = 0;
                expected.dimjogang = std::f64::consts::FRAC_PI_2;
                expected.dimtxtdirection = false;
                expected.annotative = false;
                expected.dimpost = "修改<>".into();
            }
            if stage == "omitted" && name == "Neighbor" {
                expected = DimStyle::dxf_defaults(name);
                expected.dimtxsty_handle = d.text_styles.get("Standard").unwrap().handle;
            }
            expected.handle = a.handle;
            assert_eq!(a, &expected, "{stage} {name}");
        }
    }
    println!("Rust verified complete DXF DIMSTYLE fields, duplicate group 70 semantics, handles, annotation, angle clamp and omitted defaults");
    Ok(())
}
