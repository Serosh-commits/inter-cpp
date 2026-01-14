#pragma once
#include "common/common.hpp"
#include "object/object.hpp"

using Value = std::variant<double, bool, std::nullptr_t, Obj*>;

inline bool isFalsey(const Value& v) {
    return std::holds_alternative<std::nullptr_t>(v) ||
           (std::holds_alternative<bool>(v) && !std::get<bool>(v));
}

inline bool valuesEqual(const Value& a, const Value& b) {
    return a == b;
}

inline bool isObjType(const Value& value, Obj::Type type) {
    return std::holds_alternative<Obj*>(value) && std::get<Obj*>(value)->type == type;
}

std::string valueToString(const Value& value);
