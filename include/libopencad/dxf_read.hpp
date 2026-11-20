// SPDX-License-Identifier: MPL-2.0
// Based on acadrust io/dxf/reader.rs and io/read.rs diagnostic/recovery contracts.
#pragma once
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace libopencad {
struct DxfReadOptions {
    bool failsafe = false;
    // Bounds the diagnostic reader's buffered input. Legacy read(stream) is unchanged.
    std::size_t max_bytes = 256 * 1024 * 1024;
    // Fallback for an undeclared version or a pre-R2007 HEADER without $DWGCODEPAGE.
    std::optional<std::string> default_encoding = std::nullopt;
};
struct DxfReadDiagnostic {
    std::string code, message, section, record_type, record_handle;
    std::size_t source_offset = 0;          // Zero-based file byte offset. Text errors identify the first byte of the faulty sequence.
    std::optional<std::size_t> source_line; // One-based ASCII group-code line.
};
struct DxfReadStatistics {
    bool binary = false, stream_completed = false;
    std::size_t source_sections = 0, source_records = 0;
    std::size_t retained_records = 0, skipped_records = 0, opaque_records = 0;
    std::vector<DxfReadDiagnostic> diagnostics;
    // rust ReadStats::has_usable_drawing_data: source records and a decoded payload.
    // Native retained_records is the analogue of rust decoded_source_records.
    bool has_usable_drawing_data() const {
        return source_records > 0 && retained_records > 0;
    }
    // rust ReadStats::recovered counts notification errors. Native failsafe uses skipped records.
    bool recovered() const { return skipped_records > 0; }
};
class DxfReadError : public std::runtime_error {
  public:
    explicit DxfReadError(DxfReadDiagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic_(std::move(diagnostic)) {}
    const DxfReadDiagnostic &diagnostic() const noexcept { return diagnostic_; }

  private:
    DxfReadDiagnostic diagnostic_;
};
class DxfRecoveryError : public std::runtime_error {
  public:
    explicit DxfRecoveryError(std::string details) : std::runtime_error(std::move(details)) {}
};
} // namespace libopencad
