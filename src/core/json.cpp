#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace dm {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    std::optional<JsonValue> parseDocument() {
        auto value = parseValue(0);
        skipSpace();
        if (!value || position_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    static constexpr int kMaxDepth = 32;

    void skipSpace() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\n' ||
                text_[position_] == '\r')) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skipSpace();
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool consumeWord(const char* word) {
        const std::string_view expected(word);
        if (text_.compare(position_, expected.size(), expected) != 0) return false;
        position_ += expected.size();
        return true;
    }

    std::optional<JsonValue> parseValue(int depth) {
        if (depth > kMaxDepth) return std::nullopt;
        skipSpace();
        if (position_ >= text_.size()) return std::nullopt;
        const char c = text_[position_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') {
            auto value = parseString();
            if (!value) return std::nullopt;
            return JsonValue(std::move(*value));
        }
        if (consumeWord("true")) return JsonValue(true);
        if (consumeWord("false")) return JsonValue(false);
        if (consumeWord("null")) return JsonValue();
        return parseNumber();
    }

    std::optional<JsonValue> parseObject(int depth) {
        ++position_;  // {
        JsonValue::Object object;
        if (consume('}')) return JsonValue(std::move(object));
        do {
            skipSpace();
            auto key = parseString();
            if (!key || !consume(':')) return std::nullopt;
            auto value = parseValue(depth + 1);
            if (!value) return std::nullopt;
            object[*key] = std::move(*value);
        } while (consume(','));
        if (!consume('}')) return std::nullopt;
        return JsonValue(std::move(object));
    }

    std::optional<JsonValue> parseArray(int depth) {
        ++position_;  // [
        JsonValue::Array array;
        if (consume(']')) return JsonValue(std::move(array));
        do {
            auto value = parseValue(depth + 1);
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));
        } while (consume(','));
        if (!consume(']')) return std::nullopt;
        return JsonValue(std::move(array));
    }

    std::optional<unsigned> parseHex4() {
        if (position_ + 4 > text_.size()) return std::nullopt;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    static void appendUtf8(std::string& out, unsigned codePoint) {
        if (codePoint < 0x80) {
            out += static_cast<char>(codePoint);
        } else if (codePoint < 0x800) {
            out += static_cast<char>(0xC0 | (codePoint >> 6));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codePoint >> 12));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codePoint >> 18));
            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::optional<std::string> parseString() {
        if (position_ >= text_.size() || text_[position_] != '"') return std::nullopt;
        ++position_;
        std::string result;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
            if (c != '\\') {
                result += c;
                continue;
            }
            if (position_ >= text_.size()) return std::nullopt;
            const char escape = text_[position_++];
            switch (escape) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    auto unit = parseHex4();
                    if (!unit) return std::nullopt;
                    unsigned codePoint = *unit;
                    if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {  // par substituto UTF-16
                        if (!consumeWord("\\u")) return std::nullopt;
                        auto low = parseHex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF) return std::nullopt;
                        codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    appendUtf8(result, codePoint);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        const char* start = text_.c_str() + position_;
        char* end = nullptr;
        const double value = std::strtod(start, &end);
        if (end == start || !std::isfinite(value)) return std::nullopt;
        position_ += static_cast<size_t>(end - start);
        return JsonValue(value);
    }

    const std::string& text_;
    size_t position_ = 0;
};

void serializeString(const std::string& text, std::string& out) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

}  // namespace

const JsonValue* JsonValue::field(const std::string& key) const {
    const auto* object = std::get_if<std::shared_ptr<Object>>(&data_);
    if (!object) return nullptr;
    const auto it = (*object)->find(key);
    return it == (*object)->end() ? nullptr : &it->second;
}

const JsonValue::Array* JsonValue::array() const {
    const auto* array = std::get_if<std::shared_ptr<Array>>(&data_);
    return array ? array->get() : nullptr;
}

const std::string* JsonValue::text() const {
    return std::get_if<std::string>(&data_);
}

std::string JsonValue::string(const std::string& key) const {
    const JsonValue* value = field(key);
    const std::string* text = value ? value->text() : nullptr;
    return text ? *text : std::string{};
}

std::optional<double> JsonValue::number(const std::string& key) const {
    const JsonValue* value = field(key);
    if (!value) return std::nullopt;
    if (const auto* number = std::get_if<double>(&value->data_)) return *number;
    return std::nullopt;
}

std::optional<bool> JsonValue::boolean(const std::string& key) const {
    const JsonValue* value = field(key);
    if (!value) return std::nullopt;
    if (const auto* flag = std::get_if<bool>(&value->data_)) return *flag;
    return std::nullopt;
}

std::string JsonValue::serialize() const {
    std::string out;
    if (std::holds_alternative<std::monostate>(data_)) {
        out = "null";
    } else if (const auto* flag = std::get_if<bool>(&data_)) {
        out = *flag ? "true" : "false";
    } else if (const auto* number = std::get_if<double>(&data_)) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.17g", *number);
        out = buffer;
    } else if (const auto* text = std::get_if<std::string>(&data_)) {
        serializeString(*text, out);
    } else if (const auto* object = std::get_if<std::shared_ptr<Object>>(&data_)) {
        out += '{';
        bool first = true;
        for (const auto& [key, value] : **object) {
            if (!first) out += ',';
            first = false;
            serializeString(key, out);
            out += ':';
            out += value.serialize();
        }
        out += '}';
    } else if (const auto* array = std::get_if<std::shared_ptr<Array>>(&data_)) {
        out += '[';
        for (size_t i = 0; i < (*array)->size(); ++i) {
            if (i > 0) out += ',';
            out += (**array)[i].serialize();
        }
        out += ']';
    }
    return out;
}

std::optional<JsonValue> parseJson(const std::string& text) {
    return Parser(text).parseDocument();
}

}  // namespace dm
