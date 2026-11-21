// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
namespace libopencad::detail {
// The file view must outlive the reader. The verified key belongs to this read session.
class DwgPagedReader {
  public:
    explicit DwgPagedReader(std::string_view file);
    ~DwgPagedReader();
    void authenticate(std::optional<std::string_view> password);
    bool password_protected() const;
    std::string read(std::string_view name) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string replace_ac21_sections(std::string_view file,
                                  const std::map<std::string, std::string> &replacements);
std::string replace_ac18_sections(std::string_view file,
                                  const std::map<std::string, std::string> &replacements);
} // namespace libopencad::detail
