// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Mesh;
use acadrust::{DxfReader, EntityType, Vector3};
pub fn verify(path: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(path)?.read()?;
    let cube = Mesh::create_box(Vector3::new(-1., -2., -3.), Vector3::new(2., 4., 6.));
    let plane = Mesh::create_plane(
        Vector3::new(1., 2., 3.),
        Vector3::new(4., 1., 2.),
        Vector3::new(0., 3., 1.),
        2,
        3,
    );
    let cylinder = Mesh::create_cylinder(Vector3::new(1., -2., 3.), 2.5, -4., 7, 2, true, true);
    let mut merged = cube.clone();
    merged.merge(&plane);
    merged.flip_normals();
    merged.translate(Vector3::new(2., 3., 4.));
    merged.scale(Vector3::new(1., 1., 1.), -2.);
    merged.scale_uniform(0.5);
    let expected = vec![cube, plane, cylinder, merged, Mesh::new()];
    let actual: Vec<_> = doc.entities().collect();
    assert_eq!(actual.len(), expected.len());
    for (actual, mut expected) in actual.into_iter().zip(expected) {
        expected.edges.sort_by_key(|e| (e.start, e.end));
        if let Some(e) = expected.edges.first_mut() {
            e.crease = Some(1.5);
        }
        let EntityType::Mesh(mesh) = actual else {
            panic!("Wrong type")
        };
        assert!(doc
            .block_records
            .iter()
            .any(|b| b.name == "*Model_Space" && b.handle == mesh.common.owner_handle));
        expected.common.handle = mesh.common.handle;
        expected.common.owner_handle = mesh.common.owner_handle;
        expected.common.entity_mode = mesh.common.entity_mode;
        crate::entity_transform::compare(actual, &EntityType::Mesh(expected));
    }
    Ok(())
}
