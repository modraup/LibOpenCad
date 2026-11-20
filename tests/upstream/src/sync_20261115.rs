// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::SabReader;
use acadrust::{DxfReader, EntityType};
use std::{fs, path::Path};
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for encoding in ["ascii", "binary"] {
        let d = DxfReader::from_file(format!("{prefix}.{encoding}.dxf"))?.read()?;
        let h = &d.header;
        assert_eq!(
            [
                h.user_int1,
                h.user_int2,
                h.user_int3,
                h.user_int4,
                h.user_int5
            ],
            [-1, -2, -3, -4, -5]
        );
        assert_eq!(
            [
                h.user_real1,
                h.user_real2,
                h.user_real3,
                h.user_real4,
                h.user_real5
            ],
            [1.25, 2.5, 3.75, 5., 6.25]
        );
        let mut hatches = 0;
        for e in d.entities() {
            if let EntityType::Hatch(h) = e {
                assert_eq!(h.paths.len(), 1);
                hatches += 1;
                assert!(format!("{:?}", h.paths[0]).contains("vertices: []"));
            }
        }
        assert_eq!(hatches, 1);
        for i in 0..12 {
            let d = DxfReader::from_file(format!("{prefix}.empty-{i}.{encoding}.dxf"))?.read()?;
            assert_eq!(d.entities().count(), 5);
            for e in d.entities() {
                let data = match e {
                    EntityType::Solid3D(v) => &v.acis_data,
                    EntityType::Region(v) => &v.acis_data,
                    EntityType::Body(v) => &v.acis_data,
                    EntityType::Surface(v) => &v.acis_data,
                    _ => panic!("modeler"),
                };
                assert!(!data.has_data());
                assert!(data.sat_data.is_empty());
            }
        }
    }
    // Rust reading already repaired APPID identity collisions; compare native outputs' uniqueness.
    let p = Path::new(prefix);
    let stem = p.file_name().unwrap().to_str().unwrap();
    for entry in fs::read_dir(p.parent().unwrap())? {
        let path = entry?.path();
        let name = path.file_name().unwrap().to_str().unwrap_or("");
        if !name.starts_with(&format!("{stem}.collision-")) || !name.ends_with(".dxf") {
            continue;
        }
        let d = DxfReader::from_file(path)?.read()?;
        assert!(d
            .app_ids
            .iter()
            .all(|a| d.entities().all(|e| a.handle != e.common().handle)));
    }
    let geometry = SabReader::read(&fs::read(format!("{prefix}.sab"))?).unwrap();
    assert_eq!(
        geometry.to_sat_string(),
        fs::read_to_string(format!("{prefix}.sat"))?
    );
    Ok(())
}
