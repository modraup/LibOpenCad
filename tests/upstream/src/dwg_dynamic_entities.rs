// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{EntityType, ExtendedEntityData};
use acadrust::objects::DynamicBlockData;
use acadrust::DwgReader;

pub fn verify(out: &str) -> acadrust::Result<()> {
    let restored = DwgReader::from_file(format!("{out}.dwg"))?.read()?;
    let data: Vec<_> = restored
        .entities()
        .filter_map(|entity| {
            let EntityType::Extended(entity) = entity else {
                return None;
            };
            let ExtendedEntityData::DynamicBlock(data) = &entity.data else {
                return None;
            };
            Some(data.clone())
        })
        .collect();
    assert!(
        data.iter()
            .any(|item| matches!(item, DynamicBlockData::LinearParameterEntity)),
        "empty LINEARPARAMETERENTITY"
    );
    let angular = data.iter().find_map(|item| match item {
        DynamicBlockData::AngularConstraintParameterEntity(value) => Some(value),
        _ => None,
    });
    let angular = angular.expect("angular entity");
    assert_eq!(angular.expression_name, "EditedAngle");
    assert!((angular.angle - 0.75).abs() < 1e-12, "{}", angular.angle);
    Ok(())
}
