// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::EntityType;
use acadrust::xdata::XDataValue;
use acadrust::{DwgReader, DxfReader, PreviewFormat};
use std::env;
use std::fs;
use std::path::Path;

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");

    let renamed = DxfReader::from_file(Path::new(&format!("{prefix}-rename.dxf")))
        .unwrap()
        .read()
        .unwrap();
    assert_eq!(renamed.layers.get("Renamed").unwrap().name, "Renamed");
    assert!(renamed
        .layer_state("Saved")
        .unwrap()
        .layers
        .iter()
        .any(|layer| layer.layer_name == "Renamed"));
    let entity = renamed
        .entities()
        .find(|entity| entity.common().layer.eq_ignore_ascii_case("Renamed"))
        .expect("renamed entity");
    let layer_name = entity
        .common()
        .extended_data
        .records()
        .iter()
        .find_map(|record| {
            record.values.iter().find_map(|value| match value {
                XDataValue::LayerName(name) => Some(name.as_str()),
                _ => None,
            })
        });
    assert_eq!(layer_name, Some("Renamed"));

    let reserved = DxfReader::from_file(Path::new(&format!("{prefix}-reserve.dxf")))
        .unwrap()
        .read()
        .unwrap();
    let expected: u64 = fs::read_to_string(format!("{prefix}-reserve.txt"))
        .unwrap()
        .trim()
        .parse()
        .unwrap();
    assert!(reserved.header.handle_seed >= expected);

    let mirrored = DxfReader::from_file(Path::new(&format!("{prefix}-mirror.dxf")))
        .unwrap()
        .read()
        .unwrap();
    let line = mirrored
        .entities()
        .find_map(|entity| match entity {
            EntityType::Line(line) => Some(line.clone()),
            _ => None,
        })
        .expect("mirrored line");
    assert!((line.start.z + 3.0).abs() < 1e-12);
    let text = mirrored
        .entities()
        .find_map(|entity| match entity {
            EntityType::Text(text) => Some(text.clone()),
            _ => None,
        })
        .expect("mirrored text");
    assert!((text.insertion_point.x + 14.0).abs() < 1e-12);
    assert_eq!(text.generation_flags & 2, 0);

    let graph = DxfReader::from_file(Path::new(&format!("{prefix}-block-graph.dxf")))
        .unwrap()
        .read()
        .unwrap();
    let dst = graph
        .block_records
        .get("GraphDst")
        .expect("GraphDst")
        .handle;
    assert!(graph.export_block_graph(dst).is_some());

    let preview = DwgReader::from_file(Path::new(&format!("{prefix}-preview.dwg")))
        .unwrap()
        .read()
        .unwrap();
    let image = preview.preview.expect("DWG preview");
    assert_eq!(image.format, PreviewFormat::Png);
    assert_eq!(image.data, [0x89, b'P', b'N', b'G', 9, 8, 7]);
}
