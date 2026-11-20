// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "table.hpp"
#include <string>
#include <vector>
namespace libopencad {
struct FieldChildValue {
    std::string key;
    TableValue value;
};
struct FieldDefinition {
    std::string evaluator_id, code, format, error_message, value_string;
    std::int32_t evaluation_option = 0, filing_option = 0, state = 0, evaluation_status = 0, error_code = 0,
                 value_string_length = 0;
    std::vector<std::uint64_t> children, references;
    TableValue value;
    std::vector<FieldChildValue> child_values;
    std::vector<DxfRawField> extra_fields;
};
struct FieldListDefinition {
    bool unknown = false;
    std::vector<std::uint64_t> fields;
    std::vector<DxfRawField> extra_fields;
};
// rust CadDocument::build_table_block cell arguments.
struct TableCellField {
    std::size_t row = 0, column = 0;
    std::string template_code;
    std::vector<FieldDefinition> children;
};
} // namespace libopencad
