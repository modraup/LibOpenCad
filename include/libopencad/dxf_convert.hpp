// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_version.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace libopencad {
enum class DxfConversionIssueKind { Information, Loss, Error };
struct DxfConversionIssue {
    DxfConversionIssueKind kind;
    std::string code, record_type, source_handle, message;
    int group_code = 0;
};
struct DxfConversionOptions {
    bool allow_loss = false;
    // Exact requested version by default. The optional upstream policy reports
    // Rust's promotion of pre-R2007 binary targets to R2010 explicitly.
    DxfOutputPolicy policy = DxfOutputPolicy::PreserveVersion;
    std::size_t max_output_bytes = 256 * 1024 * 1024;
};
struct DxfHandleMapping {
    std::string record_type, source_handle, target_handle;
};
struct DxfConversionReport {
    DxfVersion source = DxfVersion::Unknown, requested = DxfVersion::Unknown, effective = DxfVersion::Unknown;
    bool binary = false, can_write = false;
    std::size_t changed_records = 0;
    std::vector<DxfConversionIssue> issues;
    // R12 import identities, including child records. On refusal these describe
    // only the discarded staged conversion, not a committed output document.
    std::vector<DxfHandleMapping> handle_mappings;
};
struct DxfConversionResult {
    std::string bytes; // Empty on refusal; source document remains unchanged.
    DxfConversionReport report;
};
class DxfConversionError : public std::runtime_error {
  public:
    explicit DxfConversionError(DxfConversionReport report)
        : std::runtime_error("DXF conversion refused; inspect report()"), report_(std::move(report)) {}
    const DxfConversionReport &report() const noexcept { return report_; }

  private:
    DxfConversionReport report_;
};
} // namespace libopencad
