#pragma once

#include <string>
#include <utility>
#include <vector>

namespace SoulMod {

enum class JsonType { Null, Boolean, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    std::string stringValue;
    std::string numberValue;
    bool booleanValue = false;
    std::vector<JsonValue> arrayValues;
    std::vector<std::pair<std::string, JsonValue>> objectValues;

    const JsonValue* Find(const std::string& key) const;
};

// Parses exactly one JSON value with strict container, string-escape, and
// Unicode-surrogate validation. The package parser applies its schema to the
// resulting typed tree instead of searching raw JSON text.
bool ParseJson(const std::string& text, JsonValue& value, std::string& error);

bool ValidateJson(const std::string& text, std::string& error);

} // namespace SoulMod
