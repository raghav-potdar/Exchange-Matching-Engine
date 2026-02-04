#pragma once

#ifndef FIXMESSAGE_H
#define FIXMESSAGE_H

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

struct FixField {
    int tag{0};
    std::string value;
};

class FixMessage {
public:
    static constexpr char SOH = '\x01';

    FixMessage() = default;

    static std::optional<FixMessage> parse(const std::string& raw, std::string& error);

    void set(int tag, std::string value);
    std::optional<std::string> get(int tag) const;

    std::string serialize(const std::string& beginString) const;

    const std::vector<FixField>& fields() const { return fields_; }

private:
    std::vector<FixField> fields_;
};

#endif // FIXMESSAGE_H
