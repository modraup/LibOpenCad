// SPDX-License-Identifier: MPL-2.0
// Read statistics and recovery diagnostics follow acadrust io/read.rs and io/recover.rs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace libopencad {
enum class DwgReadStage { FileHeader, Classes, Header, Handles, Objects, RecordStream };
enum class DwgOffsetBasis { FileByte, ObjectSectionByte, HandleSectionByte };

struct DwgReadOptions {
    bool failsafe = false;
    // UTF-8 password for drawings with AcDb:Security.
    std::optional<std::string> password = std::nullopt;
};

struct DwgReadDiagnostic {
    std::string code, message, section;
    DwgReadStage stage = DwgReadStage::FileHeader;
    std::optional<std::size_t> source_offset;
    std::optional<DwgOffsetBasis> source_offset_basis;
    std::optional<std::uint64_t> record_handle;
    std::string record_type;
};

struct DwgReadStatistics {
    std::string source_version;
    std::uint8_t maintenance_version = 0;
    bool recovery_mode = false, record_stream_read = false, stream_completed = false;
    std::size_t source_sections = 0, source_records = 0;
    std::size_t decoded_source_records = 0, skipped_source_records = 0, recovered_errors = 0;
    std::size_t output_entities = 0, output_objects = 0, output_table_records = 0;
    std::vector<DwgReadDiagnostic> diagnostics;

    std::size_t output_records() const { return output_entities + output_objects + output_table_records; }
    bool has_usable_drawing_data() const {
        return record_stream_read && source_records > 0 && decoded_source_records > 0;
    }
    bool recovered() const { return recovered_errors > 0; }
};

class DwgReadError : public std::runtime_error {
  public:
    DwgReadError(std::string details, DwgReadStatistics stats)
        : std::runtime_error(std::move(details)), stats_(std::move(stats)) {}
    const DwgReadStatistics &stats() const noexcept { return stats_; }

  private:
    DwgReadStatistics stats_;
};

class DwgRecoveryError : public std::runtime_error {
  public:
    DwgRecoveryError(std::string details, DwgReadStatistics stats)
        : std::runtime_error(std::move(details)), stats_(std::move(stats)) {}
    const DwgReadStatistics &stats() const noexcept { return stats_; }

  private:
    DwgReadStatistics stats_;
};
} // namespace libopencad
