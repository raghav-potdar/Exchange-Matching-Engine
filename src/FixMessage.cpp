#include "FixMessage.h"

#include <sstream>
#include <iomanip>

std::optional<FixMessage> FixMessage::parse(const std::string& raw, std::string& error) {
    FixMessage msg;
    size_t start = 0;

    while (start < raw.size()) {
        size_t end = raw.find(SOH, start);
        if (end == std::string::npos) {
            error = "Missing SOH delimiter";
            return std::nullopt;
        }
        if (end == start) {
            start = end + 1;
            continue;
        }

        std::string field = raw.substr(start, end - start);
        size_t sep = field.find('=');
        if (sep == std::string::npos) {
            error = "Missing '=' in field";
            return std::nullopt;
        }

        std::string tagStr = field.substr(0, sep);
        std::string value = field.substr(sep + 1);
        int tag = 0;
        try {
            tag = std::stoi(tagStr);
        } catch (...) {
            error = "Invalid tag: " + tagStr;
            return std::nullopt;
        }

        msg.fields_.push_back({tag, value});
        start = end + 1;
    }

    return msg;
}

void FixMessage::set(int tag, std::string value) {
    fields_.push_back({tag, std::move(value)});
}

std::optional<std::string> FixMessage::get(int tag) const {
    for (auto it = fields_.rbegin(); it != fields_.rend(); ++it) {
        if (it->tag == tag) {
            return it->value;
        }
    }
    return std::nullopt;
}

std::string FixMessage::serialize(const std::string& beginString) const {
    std::string body;
    body.reserve(256);

    for (const auto& field : fields_) {
        if (field.tag == 8 || field.tag == 9 || field.tag == 10) {
            continue;
        }
        body += std::to_string(field.tag);
        body += '=';
        body += field.value;
        body += SOH;
    }

    std::string header;
    header.reserve(64);
    header += "8=" + beginString;
    header += SOH;
    header += "9=" + std::to_string(static_cast<int>(body.size()));
    header += SOH;

    std::string message = header + body;
    unsigned int sum = 0;
    for (unsigned char c : message) {
        sum += c;
    }
    unsigned int checksum = sum % 256;

    std::ostringstream oss;
    oss << "10=" << std::setw(3) << std::setfill('0') << checksum << SOH;
    message += oss.str();

    return message;
}
