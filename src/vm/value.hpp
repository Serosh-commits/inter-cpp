#pragma once
#include "../common/common.hpp"
#include "object/object.hpp"

using Value = std::variant<double, bool, std::nullptr_t, Obj*>;

inline bool isFalsey(const Value& v) {
    return std::holds_alternative<std::nullptr_t>(v) ||
           (std::holds_alternative<bool>(v) && !std::get<bool>(v));
}

inline bool valuesEqual(const Value& a, const Value& b) {
    return a == b;
}

std::string valueToString(const Value& value);
