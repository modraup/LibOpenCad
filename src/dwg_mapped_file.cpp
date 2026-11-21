// SPDX-License-Identifier: MPL-2.0
#include "dwg_mapped_file.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__wasm__)
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace libopencad::detail {
#if defined(_WIN32) || ((defined(__unix__) || defined(__APPLE__)) && !defined(__wasm__))
namespace {
std::size_t mapping_size(std::intmax_t length) {
    if (length < 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::ptrdiff_t>::max())
        throw std::system_error(std::make_error_code(std::errc::file_too_large), "Cannot map DWG");
    return static_cast<std::size_t>(length);
}
} // namespace
#endif

DwgMappedFile::DwgMappedFile(const std::filesystem::path &path) {
#if defined(_WIN32)
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::system_error(GetLastError(), std::system_category(), "Cannot open DWG mapping");
    const std::unique_ptr<void, decltype(&CloseHandle)> file(handle, CloseHandle);
    if (GetFileType(handle) != FILE_TYPE_DISK)
        throw std::system_error(std::make_error_code(std::errc::invalid_argument), "DWG mapping requires a regular file");
    LARGE_INTEGER length;
    if (!GetFileSizeEx(handle, &length))
        throw std::system_error(GetLastError(), std::system_category(), "Cannot measure DWG mapping");
    const auto size = mapping_size(length.QuadPart);
    if (size == 0)
        return;
    const std::unique_ptr<void, decltype(&CloseHandle)> mapping(
        CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr), CloseHandle);
    if (!mapping)
        throw std::system_error(GetLastError(), std::system_category(), "Cannot create DWG mapping");
    const auto base = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, size);
    if (!base)
        throw std::system_error(GetLastError(), std::system_category(), "Cannot map DWG");
    view_ = {static_cast<const char *>(base), size};
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__wasm__)
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd == -1)
        throw std::system_error(errno, std::generic_category(), "Cannot open DWG mapping");
    const struct File {
        int fd;
        ~File() { close(fd); }
    } file{fd};
    struct stat status;
    if (fstat(file.fd, &status) == -1)
        throw std::system_error(errno, std::generic_category(), "Cannot measure DWG mapping");
    if (!S_ISREG(status.st_mode))
        throw std::system_error(std::make_error_code(std::errc::invalid_argument), "DWG mapping requires a regular file");
    const auto size = mapping_size(status.st_size);
    if (size == 0)
        return;
    const auto base = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, file.fd, 0);
    if (base == MAP_FAILED)
        throw std::system_error(errno, std::generic_category(), "Cannot map DWG");
    view_ = {static_cast<const char *>(base), size};
#else
    (void)path;
    throw std::system_error(std::make_error_code(std::errc::not_supported), "Cannot map DWG");
#endif
}

DwgMappedFile::~DwgMappedFile() {
#if defined(_WIN32)
    if (!view_.empty())
        UnmapViewOfFile(view_.data());
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__wasm__)
    if (!view_.empty())
        munmap(const_cast<char *>(view_.data()), view_.size());
#endif
}
} // namespace libopencad::detail
