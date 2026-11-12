// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader,DxfWriter};
use std::io::Cursor;
pub fn verify(manifest:&str)->acadrust::Result<()> {
    let mut count=0;
    for line in std::fs::read_to_string(manifest)?.lines(){
        let (source,target)=line.split_once('\t').unwrap();
        let original=DxfReader::from_file(source)?.read()?;let converted=DxfReader::from_file(target)?.read()?;
        assert_eq!(original.version,converted.version);assert_eq!(original.header.insertion_units,converted.header.insertion_units);
        assert_eq!(original.entities().collect::<Vec<_>>(),converted.entities().collect::<Vec<_>>(),"{source}");
        assert_eq!(original.layers,converted.layers);assert_eq!(original.line_types,converted.line_types);
        assert_eq!(original.block_records,converted.block_records);
        // Normalize transport-specific raw numeric spelling through the same upstream writer.
        let a=DxfReader::from_reader(Cursor::new(DxfWriter::new_binary(&original).write_to_vec()?))?.read()?;
        let b=DxfReader::from_reader(Cursor::new(DxfWriter::new_binary(&converted).write_to_vec()?))?.read()?;
        assert_eq!(a.objects,b.objects,"normalized objects: {source}");count+=1;
    }
    assert_eq!(count,25);println!("Rust verified all {count} existing fixtures after transport conversion");Ok(())
}
