// SPDX-License-Identifier: MPL-2.0
#include <libopencad/object_data.hpp>
#include "dxf_code_page.hpp"
#include <cstring>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr std::uint32_t field_marker = 0x80000000u;
void push_le16(std::vector<std::uint8_t> &payload, std::uint16_t value) {
    payload.push_back(static_cast<std::uint8_t>(value & 255));
    payload.push_back(static_cast<std::uint8_t>(value >> 8));
}
void push_le32(std::vector<std::uint8_t> &payload, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        payload.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 255));
}
void push_string(std::vector<std::uint8_t> &payload, std::string_view text) {
    for (char32_t scalar : detail::utf8_scalars(text)) {
        if (scalar == 0)
            continue;
        if (scalar >= 0x10000) {
            scalar -= 0x10000;
            push_le16(payload, static_cast<std::uint16_t>(0xd800 | (scalar >> 10)));
            push_le16(payload, static_cast<std::uint16_t>(0xdc00 | (scalar & 0x3ff)));
        } else
            push_le16(payload, static_cast<std::uint16_t>(scalar));
    }
    push_le16(payload, 0);
}
std::vector<std::uint8_t> descriptor_header(std::int32_t kind, const std::string *strings) {
    std::vector<std::uint8_t> payload;
    push_le16(payload, static_cast<std::uint16_t>(kind));
    push_le16(payload, 0);
    for (int i = 0; i < 6; ++i)
        push_string(payload, strings[i]);
    return payload;
}
struct PayloadReader {
    std::string_view bytes;
    explicit PayloadReader(const std::vector<std::uint8_t> &payload)
        : bytes(reinterpret_cast<const char *>(payload.data()), payload.size()) {}
    template <class T> std::optional<T> take() {
        if (bytes.size() < sizeof(T))
            return std::nullopt;
        T value{};
        std::memcpy(&value, bytes.data(), sizeof(T));
        bytes.remove_prefix(sizeof(T));
        return value;
    }
    std::optional<std::uint16_t> u16() { return take<std::uint16_t>(); }
    std::optional<std::uint32_t> u32() { return take<std::uint32_t>(); }
    std::optional<std::int32_t> i32() { return take<std::int32_t>(); }
    std::optional<double> f64() { return take<double>(); }
    std::optional<std::string> string() {
        std::u32string scalars;
        for (;;) {
            auto unit = u16();
            if (!unit)
                return std::nullopt;
            if (*unit == 0)
                break;
            if (*unit >= 0xd800 && *unit <= 0xdbff) {
                auto next = u16();
                if (!next || *next < 0xdc00 || *next > 0xdfff)
                    return std::nullopt;
                scalars.push_back(0x10000 + ((*unit - 0xd800) << 10) + (*next - 0xdc00));
            } else if (*unit >= 0xdc00 && *unit <= 0xdfff)
                return std::nullopt;
            else
                scalars.push_back(*unit);
        }
        return detail::utf8_string(scalars);
    }
};
} // namespace
ObjectDataType ObjectDataType::real() { return {ObjectDataKind::Real, {}}; }
ObjectDataType ObjectDataType::integer() { return {ObjectDataKind::Integer, {}}; }
ObjectDataType ObjectDataType::character() { return {ObjectDataKind::Character, {}}; }
ObjectDataType ObjectDataType::from_class_name(std::string name) {
    if (name == "ClassReal")
        return real();
    if (name == "ClassLong")
        return integer();
    if (name == "ClassChar")
        return character();
    return {ObjectDataKind::Other, std::move(name)};
}
std::string ObjectDataType::class_name() const {
    if (kind == ObjectDataKind::Real)
        return "ClassReal";
    if (kind == ObjectDataKind::Integer)
        return "ClassLong";
    if (kind == ObjectDataKind::Character)
        return "ClassChar";
    return other;
}
ObjectDataValue ObjectDataValue::from_real(double value) {
    ObjectDataValue out;
    out.kind = Kind::Real;
    out.real = value;
    return out;
}
ObjectDataValue ObjectDataValue::from_integer(std::int32_t value) {
    ObjectDataValue out;
    out.kind = Kind::Integer;
    out.integer = value;
    return out;
}
ObjectDataValue ObjectDataValue::from_character(std::string value) {
    ObjectDataValue out;
    out.kind = Kind::Character;
    out.character = std::move(value);
    return out;
}
std::vector<ObjectDataDescriptor> object_data_built_in_descriptors() {
    struct BuiltIn {
        std::int32_t kind;
        const char *name;
        const char *parent;
        const char *description;
    };
    static const BuiltIn types[] = {
        {0x1501, "Class", "", "The base superclass"},
        {0x1701, "ClassNumeric", "Class", "The base class for the numerics"},
        {0x1601, "ClassReal", "ClassNumeric", "The real value"},
        {0x1601, "ClassLong", "ClassNumeric", "The long value"},
        {0x1601, "ClassChar", "Class", "The character value"},
    };
    std::vector<ObjectDataDescriptor> out;
    out.reserve(5);
    for (const auto &item : types) {
        std::string strings[6] = {item.name, "Ird", item.parent, "Ird", item.description, "Ird Class V 1.02"};
        auto payload = descriptor_header(item.kind, strings);
        push_le32(payload, 0);
        ObjectDataDescriptor value;
        value.kind = item.kind;
        value.name = std::string(item.name) + "-_-Ird";
        value.payload = std::move(payload);
        out.push_back(std::move(value));
    }
    return out;
}
ObjectDataDescriptor ObjectDataDescriptor::table(const ObjectDataTable &table) {
    std::string strings[6] = {table.name, "ade", "Class", "Ird", table.description, "1"};
    auto payload = descriptor_header(kind_table, strings);
    for (std::size_t i = 0; i < table.fields.size(); ++i) {
        auto count = i == 0 ? static_cast<std::uint32_t>(table.fields.size()) : 0u;
        push_le32(payload, field_marker | count);
        push_le16(payload, 0);
        const auto &field = table.fields[i];
        push_string(payload, field.name);
        push_string(payload, field.data_type.class_name());
        push_string(payload, "Ird");
        push_string(payload, field.description);
        push_le16(payload, 0);
    }
    push_le32(payload, 0);
    ObjectDataDescriptor value;
    value.kind = kind_table;
    value.name = table.name + "-_-ade";
    value.payload = std::move(payload);
    return value;
}
std::optional<ObjectDataTable> ObjectDataDescriptor::as_table() const {
    if (kind != kind_table)
        return std::nullopt;
    PayloadReader reader(payload);
    if (!reader.u16() || !reader.u16())
        return std::nullopt;
    auto name = reader.string();
    if (!name)
        return std::nullopt;
    for (int i = 0; i < 3; ++i)
        if (!reader.string())
            return std::nullopt;
    auto description = reader.string();
    if (!description || !reader.string())
        return std::nullopt;
    ObjectDataTable table;
    table.name = std::move(*name);
    table.description = std::move(*description);
    for (;;) {
        auto marker = reader.u32();
        if (!marker)
            return std::nullopt;
        if ((*marker & field_marker) == 0)
            return table;
        if (!reader.u16())
            return std::nullopt;
        auto field_name = reader.string();
        auto class_name = reader.string();
        if (!field_name || !class_name || !reader.string())
            return std::nullopt;
        auto field_description = reader.string();
        auto extra = reader.u16();
        if (!field_description || !extra || *extra != 0)
            return std::nullopt;
        table.fields.push_back(
            {std::move(*field_name), ObjectDataType::from_class_name(std::move(*class_name)),
             std::move(*field_description)});
    }
}
ObjectDataRecord::ObjectDataRecord(std::uint64_t table, const std::vector<ObjectDataField> &fields,
                                   const std::vector<ObjectDataValue> &values) {
    if (fields.size() != values.size())
        throw std::invalid_argument("Object Data record value count mismatch");
    this->table = table;
    push_le16(payload, 0);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto &field = fields[i];
        const auto &value = values[i];
        if (field.data_type.kind == ObjectDataKind::Real && value.kind == ObjectDataValue::Kind::Real) {
            std::uint8_t bytes[8];
            std::memcpy(bytes, &value.real, 8);
            payload.insert(payload.end(), bytes, bytes + 8);
        } else if (field.data_type.kind == ObjectDataKind::Integer &&
                   value.kind == ObjectDataValue::Kind::Integer)
            push_le32(payload, static_cast<std::uint32_t>(value.integer));
        else if (field.data_type.kind == ObjectDataKind::Character &&
                 value.kind == ObjectDataValue::Kind::Character)
            push_string(payload, value.character);
        else
            throw std::invalid_argument("Object Data field type mismatch");
    }
}
std::optional<std::vector<ObjectDataValue>>
ObjectDataRecord::values(const std::vector<ObjectDataField> &fields) const {
    PayloadReader reader(payload);
    if (!reader.u16())
        return std::nullopt;
    std::vector<ObjectDataValue> out;
    out.reserve(fields.size());
    for (const auto &field : fields) {
        if (field.data_type.kind == ObjectDataKind::Real) {
            auto value = reader.f64();
            if (!value)
                return std::nullopt;
            out.push_back(ObjectDataValue::from_real(*value));
        } else if (field.data_type.kind == ObjectDataKind::Integer) {
            auto value = reader.i32();
            if (!value)
                return std::nullopt;
            out.push_back(ObjectDataValue::from_integer(*value));
        } else if (field.data_type.kind == ObjectDataKind::Character) {
            auto value = reader.string();
            if (!value)
                return std::nullopt;
            out.push_back(ObjectDataValue::from_character(std::move(*value)));
        } else
            return std::nullopt;
    }
    return out;
}
} // namespace libopencad
