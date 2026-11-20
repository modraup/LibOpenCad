// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/compound_file.rs OLE compound storage.
#pragma once

#include "vba.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace libopencad {

struct BinaryRecord {
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> data;
    static std::vector<BinaryRecord> split(const std::uint8_t *data, std::size_t size, std::size_t chunk_size);
    static std::vector<std::uint8_t> join(const std::vector<BinaryRecord> &records);
};

struct CompoundDictionaryEntry {
    std::uint32_t property_id = 0;
    std::string name;
};

struct CompoundPropertyValue {
    enum class Kind {
        Empty,
        Null,
        I16,
        I32,
        I64,
        U16,
        U32,
        U64,
        F32,
        F64,
        Bool,
        AnsiString,
        UnicodeString,
        FileTime,
        ClassId,
        Blob,
        Clipboard,
        Dictionary,
        Vector,
        Unsupported
    };
    Kind kind = Kind::Empty;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    double real = 0;
    bool flag = false;
    std::string text;
    std::string original_text;
    std::uint16_t code_page = 1252;
    std::uint16_t original_code_page = 1252;
    std::array<std::uint8_t, 16> class_id{};
    std::vector<BinaryRecord> blob;
    std::vector<BinaryRecord> encoded;
    std::uint32_t variant_type = 0;
    std::uint32_t clipboard_format = 0;
    std::uint16_t element_type = 0;
    std::vector<CompoundDictionaryEntry> dictionary;
    std::vector<CompoundPropertyValue> values;
};

struct CompoundProperty {
    std::uint32_t id = 0;
    CompoundPropertyValue value;
};

struct CompoundPropertySet {
    std::array<std::uint8_t, 16> format_id{};
    std::vector<CompoundProperty> properties;
};

struct CompoundPropertySetStream {
    std::uint16_t version = 0;
    std::uint32_t system_identifier = 0;
    std::array<std::uint8_t, 16> class_id{};
    std::vector<CompoundPropertySet> sets;
    static std::optional<CompoundPropertySetStream> decode(const std::uint8_t *data, std::size_t size);
    std::vector<std::uint8_t> encode() const;
};

struct CompoundStreamContent {
    enum class Kind { PropertySet, VbaDirectory, VbaModule, Binary };
    Kind kind = Kind::Binary;
    CompoundPropertySetStream properties;
    VbaDirectoryStream vba_directory;
    VbaModuleStream vba_module;
    std::vector<BinaryRecord> binary;
    static CompoundStreamContent decode(std::string_view name, const std::uint8_t *data, std::size_t size);
    std::vector<std::uint8_t> encode() const;
};

struct CompoundStream {
    std::string name;
    std::array<std::uint8_t, 16> class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modified_time = 0;
    CompoundStreamContent content;
};

struct CompoundStorage;

using CompoundEntry = std::variant<CompoundStorage, CompoundStream>;

struct CompoundStorage {
    std::string name = "Root Entry";
    std::array<std::uint8_t, 16> class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modified_time = 0;
    std::vector<CompoundEntry> entries;
    static CompoundStorage root();
};

struct CompoundFile {
    std::uint16_t minor_version = 0x003E;
    std::uint16_t major_version = 3;
    std::uint32_t transaction_signature = 0;
    CompoundStorage root = CompoundStorage::root();
    static std::optional<std::pair<CompoundFile, std::size_t>> decode(const std::uint8_t *data, std::size_t size);
    std::vector<std::uint8_t> encode() const;
};

struct StructuredStoragePayload {
    std::vector<BinaryRecord> leading_records;
    std::optional<CompoundFile> compound_file;
    std::vector<BinaryRecord> trailing_records;
    static StructuredStoragePayload decode(const std::uint8_t *data, std::size_t size);
    std::vector<std::uint8_t> encode() const;
    std::size_t encoded_len() const { return encode().size(); }
};

} // namespace libopencad
