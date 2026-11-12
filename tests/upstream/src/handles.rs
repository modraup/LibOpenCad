// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, Handle};

pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for (seed, first) in [
        ("31", 0x123456789b),
        ("123456789A", 0x123456789b),
        ("1234568000", 0x1234568000),
    ] {
        let path = format!("{prefix}-{seed}");
        let mut source = DxfReader::from_file(format!("{path}.dxf"))?.read()?;
        let mut saved = DxfReader::from_file(format!("{path}-saved.dxf"))?.read()?;
        assert!(source.get_entity(Handle::new(0x31)).is_some());
        let high = Handle::new(0x123456789a);
        assert!(source.objects.contains_key(&high));
        assert_eq!(source.objects, saved.objects);
        assert_eq!(
            source.entities().collect::<Vec<_>>(),
            saved.entities().collect::<Vec<_>>()
        );
        for i in 0..4 {
            let handle = source.allocate_handle();
            assert_eq!(handle.value(), first + i);
            assert!(saved.get_entity(handle).is_none() && !saved.objects.contains_key(&handle));
        }
        assert_eq!(source.header.handle_seed, saved.header.handle_seed);
        assert_eq!(saved.header.handle_seed, first + 4);
        assert_eq!(source.allocate_handle(), saved.allocate_handle());
    }
    Ok(())
}
