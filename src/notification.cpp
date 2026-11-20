// SPDX-License-Identifier: MPL-2.0
#include <libopencad/notification.hpp>

namespace libopencad {

void NotificationCollection::notify(NotificationType type, std::string message) {
    if (items_.size() >= kMaxNotifications) {
        omitted_ += 1;
        if (type == NotificationType::Error)
            omitted_errors_ += 1;
        return;
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < message.size();) {
        unsigned char c = static_cast<unsigned char>(message[i]);
        std::size_t n = 1;
        if ((c & 0x80) == 0)
            n = 1;
        else if ((c & 0xE0) == 0xC0)
            n = 2;
        else if ((c & 0xF0) == 0xE0)
            n = 3;
        else if ((c & 0xF8) == 0xF0)
            n = 4;
        i += n;
        ++count;
        if (count > kMaxMessageChars) {
            std::string shortened;
            std::size_t taken = 0;
            for (std::size_t j = 0; j < message.size() && taken < kMaxMessageChars;) {
                unsigned char u = static_cast<unsigned char>(message[j]);
                std::size_t w = 1;
                if ((u & 0xE0) == 0xC0)
                    w = 2;
                else if ((u & 0xF0) == 0xE0)
                    w = 3;
                else if ((u & 0xF8) == 0xF0)
                    w = 4;
                shortened.append(message.substr(j, w));
                j += w;
                ++taken;
            }
            shortened += "… [message truncated]";
            message = std::move(shortened);
            break;
        }
    }
    items_.push_back(Notification::make(type, std::move(message)));
}

std::size_t NotificationCollection::error_count() const {
    std::size_t n = omitted_errors_;
    for (const auto &item : items_)
        if (item.notification_type == NotificationType::Error)
            ++n;
    return n;
}

std::vector<const Notification *> NotificationCollection::of_type(NotificationType type) const {
    std::vector<const Notification *> out;
    for (const auto &item : items_)
        if (item.notification_type == type)
            out.push_back(&item);
    return out;
}

bool NotificationCollection::has_type(NotificationType type) const {
    for (const auto &item : items_)
        if (item.notification_type == type)
            return true;
    return false;
}

void NotificationCollection::extend(NotificationCollection other) {
    for (auto &item : other.items_)
        notify(item.notification_type, std::move(item.message));
    omitted_ += other.omitted_;
    omitted_errors_ += other.omitted_errors_;
}

} // namespace libopencad
