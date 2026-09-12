#include "SoulMod/JsonValidator.hpp"

#include <cctype>
#include <cstdint>
#include <string>

namespace SoulMod {
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(value, error)) return false;
        SkipWhitespace();
        if (position_ != text_.size()) return Fail(error, "trailing content");
        return true;
    }

private:
    bool ParseValue(JsonValue& value, std::string& error) {
        SkipWhitespace();
        if (position_ == text_.size()) return Fail(error, "unexpected end of JSON");
        const char token = Peek();
        if (token == '{') return ParseObject(value, error);
        if (token == '[') return ParseArray(value, error);
        if (token == '"') { value.type = JsonType::String; return ParseString(value.stringValue, error); }
        if (token == '-' || std::isdigit(static_cast<unsigned char>(token))) { value.type = JsonType::Number; return ParseNumber(value.numberValue, error); }
        if (token == 't') { value.type = JsonType::Boolean; value.booleanValue = true; return ConsumeLiteral("true", error); }
        if (token == 'f') { value.type = JsonType::Boolean; value.booleanValue = false; return ConsumeLiteral("false", error); }
        if (token == 'n') { value.type = JsonType::Null; return ConsumeLiteral("null", error); }
        return Fail(error, "invalid JSON value");
    }

    bool ParseObject(JsonValue& value, std::string& error) {
        value.type = JsonType::Object;
        ++position_;
        SkipWhitespace();
        if (Peek() == '}') { ++position_; return true; }
        while (true) {
            if (Peek() != '"') return Fail(error, "expected string key");
            std::string key;
            if (!ParseString(key, error)) return false;
            SkipWhitespace();
            if (Peek() != ':') return Fail(error, "expected ':'");
            ++position_;
            JsonValue member;
            if (!ParseValue(member, error)) return false;
            value.objectValues.emplace_back(std::move(key), std::move(member));
            SkipWhitespace();
            if (Peek() == '}') { ++position_; return true; }
            if (Peek() != ',') return Fail(error, "expected ',' or '}'");
            ++position_;
            SkipWhitespace();
        }
    }

    bool ParseArray(JsonValue& value, std::string& error) {
        value.type = JsonType::Array;
        ++position_;
        SkipWhitespace();
        if (Peek() == ']') { ++position_; return true; }
        while (true) {
            JsonValue element;
            if (!ParseValue(element, error)) return false;
            value.arrayValues.push_back(std::move(element));
            SkipWhitespace();
            if (Peek() == ']') { ++position_; return true; }
            if (Peek() != ',') return Fail(error, "expected ',' or ']'");
            ++position_;
            SkipWhitespace();
        }
    }

    bool ParseString(std::string& value, std::string& error) {
        ++position_;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') return true;
            if (static_cast<unsigned char>(character) < 0x20) return Fail(error, "control character in string");
            if (character != '\\') { value += character; continue; }
            if (position_ == text_.size()) return Fail(error, "unterminated escape sequence");
            switch (text_[position_++]) {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'u': if (!ParseUnicodeEscape(value, error)) return false; break;
            default: return Fail(error, "invalid escape sequence");
            }
        }
        return Fail(error, "unterminated string");
    }

    bool ParseUnicodeEscape(std::string& value, std::string& error) {
        uint32_t codepoint = 0;
        if (!ParseHexQuad(codepoint, error)) return false;
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u') return Fail(error, "unpaired high surrogate");
            position_ += 2;
            uint32_t low = 0;
            if (!ParseHexQuad(low, error)) return false;
            if (low < 0xDC00 || low > 0xDFFF) return Fail(error, "invalid low surrogate");
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return Fail(error, "unpaired low surrogate");
        }
        AppendUtf8(value, codepoint);
        return true;
    }

    bool ParseHexQuad(uint32_t& value, std::string& error) {
        if (position_ + 4 > text_.size()) return Fail(error, "incomplete Unicode escape");
        value = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = text_[position_++];
            value <<= 4;
            if (character >= '0' && character <= '9') value |= static_cast<uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f') value |= static_cast<uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') value |= static_cast<uint32_t>(character - 'A' + 10);
            else return Fail(error, "invalid Unicode escape");
        }
        return true;
    }

    bool ParseNumber(std::string& value, std::string& error) {
        const size_t start = position_;
        if (Peek() == '-') ++position_;
        if (Peek() == '0') ++position_;
        else if (std::isdigit(static_cast<unsigned char>(Peek()))) while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_;
        else return Fail(error, "invalid number");
        if (Peek() == '.') { ++position_; if (!std::isdigit(static_cast<unsigned char>(Peek()))) return Fail(error, "invalid fraction"); while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_; }
        if (Peek() == 'e' || Peek() == 'E') { ++position_; if (Peek() == '+' || Peek() == '-') ++position_; if (!std::isdigit(static_cast<unsigned char>(Peek()))) return Fail(error, "invalid exponent"); while (std::isdigit(static_cast<unsigned char>(Peek()))) ++position_; }
        value = text_.substr(start, position_ - start);
        return true;
    }

    bool ConsumeLiteral(const char* literal, std::string& error) {
        const size_t length = std::char_traits<char>::length(literal);
        if (text_.compare(position_, length, literal) != 0) return Fail(error, "invalid literal");
        position_ += length;
        return true;
    }

    static void AppendUtf8(std::string& value, uint32_t codepoint) {
        if (codepoint <= 0x7F) value += static_cast<char>(codepoint);
        else if (codepoint <= 0x7FF) { value += static_cast<char>(0xC0 | (codepoint >> 6)); value += static_cast<char>(0x80 | (codepoint & 0x3F)); }
        else if (codepoint <= 0xFFFF) { value += static_cast<char>(0xE0 | (codepoint >> 12)); value += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)); value += static_cast<char>(0x80 | (codepoint & 0x3F)); }
        else { value += static_cast<char>(0xF0 | (codepoint >> 18)); value += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)); value += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)); value += static_cast<char>(0x80 | (codepoint & 0x3F)); }
    }

    bool Fail(std::string& error, const char* message) const { error = std::string(message) + " at byte " + std::to_string(position_); return false; }
    void SkipWhitespace() { while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_; }
    char Peek() const { return position_ < text_.size() ? text_[position_] : '\0'; }

    const std::string& text_;
    size_t position_ = 0;
};

}

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (type != JsonType::Object) return nullptr;
    for (const auto& [memberKey, memberValue] : objectValues) if (memberKey == key) return &memberValue;
    return nullptr;
}

bool ParseJson(const std::string& text, JsonValue& value, std::string& error) {
    error.clear();
    if (text.empty()) { error = "empty JSON document"; return false; }
    value = JsonValue{};
    return JsonParser(text).Parse(value, error);
}

bool ValidateJson(const std::string& text, std::string& error) {
    JsonValue ignored;
    return ParseJson(text, ignored, error);
}

}
