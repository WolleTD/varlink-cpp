/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_MESSAGE_HPP
#define LIBVARLINK_VARLINK_MESSAGE_HPP

#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>

#include "varlink/common.hpp"

namespace varlink {

    class Message {
        json json_;
        bool hasmore{false};
        bool hasoneway{false};
    public:
        Message() = default;
        explicit Message(const json& msg) : json_(msg) {
            if (!json_.is_object() || !json_.contains("method") || !json_["method"].is_string()) {
                throw std::invalid_argument(msg.dump());
            }
            if (!json_.contains("parameters")) {
                json_.emplace("parameters", json::object());
            } else if(!json_["parameters"].is_object()) {
                throw(std::invalid_argument("Not a varlink message: " + msg.dump()));
            }
            hasmore = (msg.contains("more") && msg["more"].get<bool>());
            hasoneway = (msg.contains("oneway") && msg["oneway"].get<bool>());
        }
        [[nodiscard]] bool more() const { return hasmore; }
        [[nodiscard]] bool oneway() const { return hasoneway; }
        [[nodiscard]] const json& parameters() const { return json_["parameters"]; }
        [[nodiscard]] std::pair<std::string, std::string> interfaceAndMethod() const {
            const auto& fqmethod = json_["method"].get<std::string>();
            const auto dot = fqmethod.rfind('.');
            // When there is no dot at all, both fields contain the same value, but it's an invalid
            // interface name anyway
            return {fqmethod.substr(0, dot), fqmethod.substr(dot + 1)};
        }

    };

}
#endif /* LIBVARLINK_VARLINK_MESSAGE_HPP */