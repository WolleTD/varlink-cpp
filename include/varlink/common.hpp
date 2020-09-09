/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_COMMON_HPP
#define LIBVARLINK_VARLINK_COMMON_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace varlink {
    using nlohmann::json;
    using MethodCallback = std::function<json(const json&, const std::function<void(json)>&, bool)>;
    using CallbackMap = std::unordered_map<std::string, MethodCallback>;
}

#endif // LIBVARLINK_VARLINK_COMMON_HPP
