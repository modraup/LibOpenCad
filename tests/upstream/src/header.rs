// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut metric = CadDocument::with_version(DxfVersion::AC1018);
    metric.header.insertion_units = 4;
    DxfWriter::new(&metric).write_to_file(ascii)?;
    let mut imperial = CadDocument::with_version(DxfVersion::AC1027);
    imperial.header.insertion_units = 1;
    DxfWriter::new_binary(&imperial).write_to_file(binary)
}

pub fn verify(drawing: &str, cpp_result: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(drawing)?.read()?;
    let mut rust_result = format!("{} {}", doc.version.as_str(), doc.header.insertion_units);
    for name in doc.header.dxf_dim_variables.as_ref().unwrap() {
        rust_result.push('\n');
        rust_result.push_str(name);
    }
    assert_eq!(std::fs::read_to_string(cpp_result)?.trim_end(), rust_result);
    println!("{drawing}: C++ and Rust both read {rust_result}");
    Ok(())
}
