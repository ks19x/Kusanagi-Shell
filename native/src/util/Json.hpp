// Json.hpp — small JSON value + parser + writer (colors.json, settings.json migration, niri/hyprctl
// output, the IPC wire format). Objects keep insertion order.
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ks::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v = nullptr;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(double d) : v(d) {}
    Value(int i) : v(double(i)) {}
    Value(int64_t i) : v(double(i)) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(Array a) : v(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool isBool() const { return std::holds_alternative<bool>(v); }
    bool isNumber() const { return std::holds_alternative<double>(v); }
    bool isString() const { return std::holds_alternative<std::string>(v); }
    bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(v); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(v); }

    bool boolean(bool def = false) const { return isBool() ? std::get<bool>(v) : def; }
    double number(double def = 0) const { return isNumber() ? std::get<double>(v) : def; }
    int64_t integer(int64_t def = 0) const { return isNumber() ? int64_t(std::get<double>(v)) : def; }
    const std::string& string() const;
    const Array& array() const;
    const Object& object() const;

    const Value& operator[](std::string_view key) const;   // null Value if missing / not an object
    const Value& operator[](size_t i) const;
    bool has(std::string_view key) const;
    void set(std::string key, Value val);                   // object only
};

std::optional<Value> parse(std::string_view text, std::string* error = nullptr);
std::string dump(const Value& v, int indent = -1);
std::string quote(std::string_view s);

} // namespace ks::json
