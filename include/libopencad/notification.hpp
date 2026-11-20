// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/notification.rs.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace libopencad {

enum class NotificationType { NotImplemented, NotSupported, Warning, Error };

inline const char *notification_type_name(NotificationType type) {
    switch (type) {
    case NotificationType::NotImplemented:
        return "NotImplemented";
    case NotificationType::NotSupported:
        return "NotSupported";
    case NotificationType::Warning:
        return "Warning";
    case NotificationType::Error:
        return "Error";
    }
    return "Warning";
}

struct Notification {
    NotificationType notification_type = NotificationType::Warning;
    std::string message;
    static Notification make(NotificationType type, std::string message) {
        return {type, std::move(message)};
    }
    std::string to_string() const {
        return std::string("[") + notification_type_name(notification_type) + "] " + message;
    }
};

class NotificationCollection {
public:
    static constexpr std::size_t kMaxNotifications = 1000;
    static constexpr std::size_t kMaxMessageChars = 2000;

    void notify(NotificationType type, std::string message);
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    std::size_t omitted_count() const { return omitted_; }
    std::size_t error_count() const;
    const std::vector<Notification> &items() const { return items_; }
    std::vector<const Notification *> of_type(NotificationType type) const;
    bool has_type(NotificationType type) const;
    void extend(NotificationCollection other);
    std::vector<Notification> into_vec() && { return std::move(items_); }

private:
    std::vector<Notification> items_;
    std::size_t omitted_ = 0;
    std::size_t omitted_errors_ = 0;
};

} // namespace libopencad
