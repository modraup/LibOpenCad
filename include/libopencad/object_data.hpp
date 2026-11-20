// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace libopencad {
// Map 3D Object Data payloads from acadrust objects/object_data.rs.
inline constexpr const char *object_data_dictionary_key = "IrdDscDictionary";
inline std::string object_data_next_key(const std::vector<std::pair<std::string, std::uint64_t>> &entries) {
    std::uint32_t last = 0;
    for (const auto &entry : entries) {
        if (entry.first.size() < 3 || entry.first[0] != '*' || entry.first[1] != 'A')
            continue;
        char *end = nullptr;
        auto n = std::strtoul(entry.first.c_str() + 2, &end, 10);
        if (end && *end == '\0' && n <= 0xffffffffu && static_cast<std::uint32_t>(n) > last)
            last = static_cast<std::uint32_t>(n);
    }
    return "*A" + std::to_string(last + 1);
}
enum class ObjectDataKind { Real, Integer, Character, Other };
struct ObjectDataType {
    ObjectDataKind kind = ObjectDataKind::Other;
    std::string other;
    static ObjectDataType real();
    static ObjectDataType integer();
    static ObjectDataType character();
    static ObjectDataType from_class_name(std::string name);
    std::string class_name() const;
};
inline bool operator==(const ObjectDataType &a, const ObjectDataType &b) {
    return a.kind == b.kind && a.other == b.other;
}
struct ObjectDataField {
    std::string name;
    ObjectDataType data_type;
    std::string description;
};
inline bool operator==(const ObjectDataField &a, const ObjectDataField &b) {
    return a.name == b.name && a.data_type == b.data_type && a.description == b.description;
}
struct ObjectDataTable {
    std::string name;
    std::string description;
    std::vector<ObjectDataField> fields;
};
inline bool operator==(const ObjectDataTable &a, const ObjectDataTable &b) {
    return a.name == b.name && a.description == b.description && a.fields == b.fields;
}
struct ObjectDataValue {
    enum class Kind { Real, Integer, Character } kind = Kind::Integer;
    double real = 0;
    std::int32_t integer = 0;
    std::string character;
    static ObjectDataValue from_real(double value);
    static ObjectDataValue from_integer(std::int32_t value);
    static ObjectDataValue from_character(std::string value);
};
inline bool operator==(const ObjectDataValue &a, const ObjectDataValue &b) {
    if (a.kind != b.kind)
        return false;
    if (a.kind == ObjectDataValue::Kind::Real)
        return a.real == b.real;
    if (a.kind == ObjectDataValue::Kind::Integer)
        return a.integer == b.integer;
    return a.character == b.character;
}
struct ObjectDataDictionary {
    std::vector<std::pair<std::string, std::uint64_t>> entries;
    std::int16_t duplicate_cloning = 1;
    bool hard_owner = false;
};
struct ObjectDataDescriptor {
    static constexpr std::int32_t kind_table = 0x3000;
    std::int32_t kind = 0;
    std::string name;
    std::vector<std::uint8_t> payload;
    static ObjectDataDescriptor table(const ObjectDataTable &table);
    std::optional<ObjectDataTable> as_table() const;
};
std::vector<ObjectDataDescriptor> object_data_built_in_descriptors();
struct ObjectDataRecord {
    std::uint64_t table = 0;
    std::vector<std::uint8_t> payload;
    ObjectDataRecord() = default;
    ObjectDataRecord(std::uint64_t table, const std::vector<ObjectDataField> &fields,
                     const std::vector<ObjectDataValue> &values);
    std::optional<std::vector<ObjectDataValue>> values(const std::vector<ObjectDataField> &fields) const;
};
struct ObjectDataAttachment {
    std::uint64_t record = 0;
    std::uint64_t table = 0;
    std::vector<ObjectDataValue> values;
};
} // namespace libopencad
