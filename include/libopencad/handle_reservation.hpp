// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/document/handle_reservation.rs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace libopencad {

class HandleReservationError : public std::runtime_error {
  public:
    enum class Kind { InconsistentDocument, UninspectableData, Exhausted };

    Kind kind() const noexcept { return kind_; }
    const std::string &field_path() const noexcept { return field_path_; }
    std::optional<std::uint64_t> handle() const noexcept { return handle_; }

    static HandleReservationError exhausted() {
        return HandleReservationError(Kind::Exhausted, "CAD handle tail range exhausted", {}, {});
    }
    static HandleReservationError uninspectable(std::string path) {
        return HandleReservationError(Kind::UninspectableData, "uninspectable handle carrier",
                                      std::move(path), {});
    }
    static HandleReservationError inconsistent(std::string path, std::optional<std::uint64_t> handle) {
        return HandleReservationError(Kind::InconsistentDocument, "inconsistent identity",
                                      std::move(path), handle);
    }

  private:
    HandleReservationError(Kind kind, const char *what, std::string path,
                           std::optional<std::uint64_t> handle)
        : std::runtime_error(what), kind_(kind), field_path_(std::move(path)), handle_(handle) {}
    Kind kind_;
    std::string field_path_;
    std::optional<std::uint64_t> handle_;
};

// Already consumed identities belonging to one document's allocation history.
// Dropping this value does not release its numbers.
class HandleReservation {
  public:
    HandleReservation() = default;
    HandleReservation(std::uint64_t start, std::uint64_t end, std::size_t count)
        : start_(start), end_(end), count_(count) {}

    std::size_t len() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    bool is_empty() const noexcept { return empty(); }
    std::optional<std::uint64_t> get(std::size_t index) const noexcept {
        if (index >= count_)
            return std::nullopt;
        return start_ + static_cast<std::uint64_t>(index);
    }
    std::uint64_t start() const noexcept { return start_; }
    std::uint64_t end() const noexcept { return end_; }

  private:
    std::uint64_t start_ = 0, end_ = 0;
    std::size_t count_ = 0;
};

} // namespace libopencad
