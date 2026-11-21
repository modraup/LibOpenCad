// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <filesystem>
#include <string_view>

namespace libopencad::detail {
class DwgMappedFile {
  public:
    explicit DwgMappedFile(const std::filesystem::path &path);
    ~DwgMappedFile();
    DwgMappedFile(const DwgMappedFile &) = delete;
    DwgMappedFile &operator=(const DwgMappedFile &) = delete;

    std::string_view view() const noexcept { return view_; }

  private:
    std::string_view view_;
};
} // namespace libopencad::detail
