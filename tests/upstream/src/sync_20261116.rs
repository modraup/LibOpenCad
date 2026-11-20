// SPDX-License-Identifier: MPL-2.0
// Standalone oracle for the local upstream checkout (08b87046 or newer).
#[path = "mtext_format.rs"]
mod mtext_format;
use acadrust::{DxfReader, EntityType};
fn main() -> acadrust::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    mtext_format::verify(&args[1], &args[2])?;
    for encoding in ["ascii", "binary"] {
        let d = DxfReader::from_file(format!("{}-{encoding}.dxf", args[3]))?.read()?;
        assert_eq!(d.wipeout_frame(), 2);
        assert_eq!(d.objects.values().filter(|o| matches!(o, acadrust::objects::ObjectType::WipeoutVariables(_))).count(), 1);
        for e in d.entities() {
            if let EntityType::Wipeout(w) = e { assert_ne!(w.flags.bits() & 1, 0); }
        }
    }
    println!("Drawing-wide wipeout frames match Rust for ASCII and binary DXF");
    Ok(())
}
