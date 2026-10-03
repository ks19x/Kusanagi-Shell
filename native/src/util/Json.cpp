#include "util/Json.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace ks::json {
namespace {

const Value kNull;
const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }
    bool fail(std::string_view what) {
        if (err.empty()) err = std::format("{} at offset {}", what, i);
        return false;
    }
    static void utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& out) {
        if (i + 4 > s.size()) return fail("short \\u escape");
        out = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') out |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool str(std::string& out) {
        i++;   // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    uint32_t lo;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& out, int depth) {
        if (depth > 256) return fail("too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            i++;
            Object obj;
            ws();
            if (i < s.size() && s[i] == '}') { i++; out = Value(std::move(obj)); return true; }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected key");
                std::string key;
                if (!str(key)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                i++;
                Value v;
                if (!value(v, depth + 1)) return false;
                obj.emplace_back(std::move(key), std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                return fail("expected ',' or '}'");
            }
            out = Value(std::move(obj));
            return true;
        }
        if (c == '[') {
            i++;
            Array arr;
            ws();
            if (i < s.size() && s[i] == ']') { i++; out = Value(std::move(arr)); return true; }
            for (;;) {
                Value v;
                if (!value(v, depth + 1)) return false;
                arr.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                return fail("expected ',' or ']'");
            }
            out = Value(std::move(arr));
            return true;
        }
        if (c == '"') {
            std::string v;
            if (!str(v)) return false;
            out = Value(std::move(v));
            return true;
        }
        if (s.substr(i, 4) == "true") { i += 4; out = Value(true); return true; }
        if (s.substr(i, 5) == "false") { i += 5; out = Value(false); return true; }
        if (s.substr(i, 4) == "null") { i += 4; out = Value(nullptr); return true; }
        size_t start = i;
        while (i < s.size() && (std::isdigit(uint8_t(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
        double d = 0;
        auto [p, ec] = std::from_chars(s.data() + start, s.data() + i, d);
        if (ec != std::errc() || start == i) return fail("bad value");
        out = Value(d);
        return true;
    }
};

void dumpTo(std::string& out, const Value& v, int indent, int level) {
    auto nl = [&](int l) {
        if (indent < 0) return;
        out += '\n';
        out.append(size_t(indent * l), ' ');
    };
    if (v.isNull()) out += "null";
    else if (v.isBool()) out += v.boolean() ? "true" : "false";
    else if (v.isNumber()) {
        double d = v.number();
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) out += std::format("{}", int64_t(d));
        else if (std::isfinite(d)) out += std::format("{}", d);
        else out += "null";
    } else if (v.isString()) out += quote(v.string());
    else if (v.isArray()) {
        auto& a = v.array();
        out += '[';
        for (size_t k = 0; k < a.size(); k++) {
            if (k) out += ',';
            nl(level + 1);
            dumpTo(out, a[k], indent, level + 1);
        }
        if (!a.empty()) nl(level);
        out += ']';
    } else {
        auto& o = v.object();
        out += '{';
        for (size_t k = 0; k < o.size(); k++) {
            if (k) out += ',';
            nl(level + 1);
            out += quote(o[k].first);
            out += indent < 0 ? ":" : ": ";
            dumpTo(out, o[k].second, indent, level + 1);
        }
        if (!o.empty()) nl(level);
        out += '}';
    }
}

} // namespace

const std::string& Value::string() const { return isString() ? std::get<std::string>(v) : kEmptyString; }
const Array& Value::array() const { return isArray() ? *std::get<std::shared_ptr<Array>>(v) : kEmptyArray; }
const Object& Value::object() const { return isObject() ? *std::get<std::shared_ptr<Object>>(v) : kEmptyObject; }

const Value& Value::operator[](std::string_view key) const {
    for (auto& [k, val] : object())
        if (k == key) return val;
    return kNull;
}
const Value& Value::operator[](size_t i) const {
    auto& a = array();
    return i < a.size() ? a[i] : kNull;
}
bool Value::has(std::string_view key) const {
    for (auto& [k, val] : object())
        if (k == key) return true;
    return false;
}
void Value::set(std::string key, Value val) {
    if (!isObject()) v = std::make_shared<Object>();
    auto& o = *std::get<std::shared_ptr<Object>>(v);
    for (auto& [k, old] : o)
        if (k == key) { old = std::move(val); return; }
    o.emplace_back(std::move(key), std::move(val));
}

std::optional<Value> parse(std::string_view text, std::string* error) {
    Parser p{text};
    Value v;
    if (!p.value(v, 0)) {
        if (error) *error = p.err;
        return std::nullopt;
    }
    p.ws();
    if (p.i != text.size()) {
        if (error) *error = std::format("trailing data at offset {}", p.i);
        return std::nullopt;
    }
    return v;
}

std::string dump(const Value& v, int indent) {
    std::string out;
    dumpTo(out, v, indent, 0);
    return out;
}

std::string quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (uint8_t(c) < 0x20) out += std::format("\\u{:04x}", int(c));
            else out += c;
        }
    }
    out += '"';
    return out;
}

} // namespace ks::json
