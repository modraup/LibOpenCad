// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{EntityType, ExtendedEntityData};
use acadrust::objects::DynamicBlockData;
use acadrust::DxfReader;

fn dynamic_data(doc: &acadrust::CadDocument) -> Vec<DynamicBlockData> {
    doc.entities()
        .filter_map(|entity| {
            let EntityType::Extended(entity) = entity else {
                return None;
            };
            let ExtendedEntityData::DynamicBlock(data) = &entity.data else {
                return None;
            };
            Some(data.clone())
        })
        .collect()
}

pub fn verify(out: &str) -> acadrust::Result<()> {
    for suffix in ["-ascii.dxf", "-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        let classes = restored.classes.get_by_name("LINEARPARAMETERENTITY");
        assert!(
            classes.is_some_and(|class| class.is_an_entity),
            "{out}{suffix}: LINEARPARAMETERENTITY class"
        );
        assert!(
            restored
                .classes
                .get_by_name("BLOCKANGULARCONSTRAINTPARAMETERENTITY")
                .is_some_and(|class| class.is_an_entity),
            "{out}{suffix}: angular class"
        );
        let data = dynamic_data(&restored);
        assert!(
            data.iter()
                .any(|item| matches!(item, DynamicBlockData::LinearParameterEntity)),
            "{out}{suffix}: empty LINEARPARAMETERENTITY"
        );
        let angular = data.iter().find_map(|item| match item {
            DynamicBlockData::AngularConstraintParameterEntity(value) => Some(value),
            _ => None,
        });
        let angular = angular.expect("angular entity");
        assert_eq!(angular.expression_name, "EditedAngle");
        assert!((angular.angle - 1.25).abs() < 1e-12, "{}", angular.angle);
    }
    Ok(())
}
