// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/vba.rs VBA project streams.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace libopencad {

struct BinaryRecord;

struct VbaDirectoryValue {
    enum class Kind { Marker, U16, U32, ProjectVersion, AnsiString, UnicodeString, Binary };
    Kind kind = Kind::Marker;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint32_t major = 0;
    std::uint16_t minor = 0;
    std::string value;
    std::string original_value;
    std::uint16_t original_code_page = 1252;
    std::vector<std::uint8_t> encoded;
};

struct VbaDirectoryRecord {
    std::uint16_t id = 0;
    VbaDirectoryValue value;
};

struct VbaModuleDescriptor {
    std::string name;
    std::string stream_name;
    std::uint32_t source_offset = 0;
    std::uint16_t code_page = 1252;
};

struct VbaDirectoryStream {
    std::uint16_t code_page = 1252;
    std::vector<VbaDirectoryRecord> records;
    std::uint16_t original_code_page = 1252;
    std::vector<VbaDirectoryRecord> original_records;
    std::vector<std::uint8_t> compressed;
    static std::optional<VbaDirectoryStream> decode(const std::uint8_t *data, std::size_t size);
    std::vector<std::uint8_t> encode() const;
    std::vector<VbaModuleDescriptor> module_descriptors() const;
};

struct VbaModuleStream {
    std::uint32_t source_offset = 0;
    std::uint16_t code_page = 1252;
    std::vector<std::uint8_t> performance_cache;
    std::string source;
    std::uint32_t original_source_offset = 0;
    std::uint16_t original_code_page = 1252;
    std::string original_source;
    std::vector<std::uint8_t> compressed_source;
    static std::optional<VbaModuleStream> decode(const std::uint8_t *data, std::size_t size,
                                                 std::uint32_t source_offset, std::uint16_t code_page);
    std::vector<std::uint8_t> encode() const;
};

std::optional<std::vector<std::uint8_t>> vba_decompress_container(const std::uint8_t *data, std::size_t size);
std::vector<std::uint8_t> vba_compress_container(const std::uint8_t *data, std::size_t size);

} // namespace libopencad
