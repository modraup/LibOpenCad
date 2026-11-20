// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 layer states).
use acadrust::layer_state::LayerStateMask;
use acadrust::types::{Color, Transparency};
use acadrust::DxfReader;
use std::env;
use std::path::Path;

fn read_dxf(path: &Path) -> acadrust::CadDocument {
    DxfReader::from_file(path).unwrap().read().unwrap()
}

fn file(prefix: &str, stem: &str) -> std::path::PathBuf {
    Path::new(&format!("{prefix}-{stem}.dxf")).to_path_buf()
}

fn named_color(doc: &acadrust::CadDocument) -> &acadrust::tables::Layer {
    doc.layers.get("Named color").expect("Named color")
}

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");

    let captured = read_dxf(&file(&prefix, "captured"));
    let state = captured.layer_state("named state").expect("named state");
    assert_eq!(state.name, "named state");
    assert_eq!(state.description, "updated");
    assert_eq!(state.mask, LayerStateMask::SUPPORTED);
    assert_eq!(state.current_layer, "0");
    assert_eq!(state.layers.len(), 3);
    let book = state
        .layers
        .iter()
        .find(|layer| layer.layer_name == "Named color")
        .expect("captured Named color");
    assert_eq!(book.color, Color::from_rgb(12, 34, 56));
    assert_eq!(book.color_name.as_deref(), Some("Accent"));
    assert_eq!(book.book_name.as_deref(), Some("Brand colors"));
    assert!(book.plot_style.starts_with("Color_"));
    assert_eq!(book.transparency, Some(Transparency::OPAQUE));
    let frozen = state
        .layers
        .iter()
        .find(|layer| layer.layer_name == "Frozen")
        .expect("captured Frozen");
    assert!(frozen.off && frozen.frozen);
    assert_eq!(frozen.color, Color::from_index(5));

    let mut rust = captured;
    assert_eq!(rust.restore_layer_state("named state"), Some(3));
    let layer = rust.layers.get("Named color").expect("restored rust layer");
    assert_eq!(layer.color, Color::from_rgb(12, 34, 56));
    assert_eq!(layer.color_name.as_deref(), Some("Accent"));
    assert_eq!(layer.book_name.as_deref(), Some("Brand colors"));

    let restored = read_dxf(&file(&prefix, "restored"));
    let layer = named_color(&restored);
    assert_eq!(layer.color, Color::from_rgb(12, 34, 56));
    assert_eq!(layer.color_name.as_deref(), Some("Accent"));
    assert_eq!(layer.book_name.as_deref(), Some("Brand colors"));
    let frozen = restored.layers.get("Frozen").expect("restored Frozen");
    assert!(frozen.flags.off && frozen.flags.frozen);
    assert_eq!(frozen.color, Color::from_index(5));

    let renamed = read_dxf(&file(&prefix, "renamed"));
    assert!(renamed.layer_state("Renamed").is_some());
    assert!(renamed.layer_state("named state").is_none());
    assert!(renamed.layer_state("Other").is_some());

    let gone = read_dxf(&file(&prefix, "gone"));
    assert!(gone.layer_states().is_empty());
}
