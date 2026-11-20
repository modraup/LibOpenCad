// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace libopencad {
enum class R12IssueKind { Information, Loss, Error };
struct R12Issue {
    R12IssueKind kind;
    std::string code, record_type, source_handle, message;
};
struct R12NameMapping {
    std::string table, source, target;
};
struct R12ExportOptions {
    bool allow_loss = false;
    // Requires allow_loss as well. Invalid data/ambiguous identity always rejects.
    bool omit_unsupported_entities = false;
    std::size_t max_output_bytes = 256 * 1024 * 1024;
};
struct R12ExportReport {
    bool can_write = false;
    std::size_t source_entities = 0, output_entities = 0, converted_entities = 0, omitted_entities = 0;
    std::size_t removed_objects = 0;
    std::vector<R12Issue> issues;
    std::vector<R12NameMapping> names;
};
struct R12ExportResult {
    // Empty on refusal. The source document is never mutated.
    std::string ascii;
    R12ExportReport report;
};
class R12ExportError : public std::runtime_error {
  public:
    explicit R12ExportError(R12ExportReport report)
        : std::runtime_error("R12 export refused; inspect report()"), report_(std::move(report)) {}
    const R12ExportReport &report() const noexcept { return report_; }

  private:
    R12ExportReport report_;
};
} // namespace libopencad
