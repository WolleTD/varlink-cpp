/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_MESSAGE_HPP
#define LIBVARLINK_VARLINK_MESSAGE_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>

#include "varlink/common.hpp"

namespace varlink {

    class Message : json {
        bool hasmore{false};
        bool hasoneway{false};
    public:
        Message() = default;
        explicit Message(const json& msg) : json(msg) {
            if (!is_object() || !contains("method")) {
                throw std::invalid_argument(msg.dump());
            }
            if (!contains("parameters")) {
                emplace("parameters", json::object());
            }
            hasmore = (msg.contains("more") && msg["more"].get<bool>());
            hasoneway = (msg.contains("oneway") && msg["oneway"].get<bool>());
        }
        [[nodiscard]] bool more() const { return hasmore; }
        [[nodiscard]] bool oneway() const { return hasoneway; }
        [[nodiscard]] const json& parameters() const { return *find("parameters"); }
        [[nodiscard]] std::pair<std::string, std::string> interfaceAndMethod() const {
            const auto& fqmethod = find("method")->get<std::string>();
            const auto dot = fqmethod.rfind('.');
            // When there is no dot at all, both fields contain the same value, but it's an invalid
            // interface name anyway
            return {fqmethod.substr(0, dot), fqmethod.substr(dot + 1)};
        }

    };

}
#endif /* LIBVARLINK_VARLINK_MESSAGE_HPP */