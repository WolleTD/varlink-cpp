/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_COMMON_HPP
#define LIBVARLINK_VARLINK_COMMON_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <exception>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>

namespace varlink {
    using nlohmann::json;
    using MethodCallback = std::function<json(const json&, const std::function<void(json)>&)>;
    using CallbackMap = std::unordered_map<std::string, MethodCallback>;

    using SendMore = std::function<void(json)>;

    class varlink_error : public std::logic_error {
        json _args;
    public:
        varlink_error(const std::string& what, json args) : std::logic_error(what), _args(std::move(args)) {}
        [[nodiscard]] const json& args() const { return _args; }
    };
}

#endif // LIBVARLINK_VARLINK_COMMON_HPP
