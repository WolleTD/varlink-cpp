/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <charconv>
#include <string>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
enum class call_mode {
    basic,
    oneway,
    more,
    upgrade,
};

class varlink_client {
   private:
    json_connection_variant conn;

   public:
    explicit varlink_client(const varlink_uri &uri) : conn(make_from_uri<basic_json_connection>(uri)) {}
    explicit varlink_client(std::string_view uri) : varlink_client(varlink_uri(uri)) {}
    explicit varlink_client(json_connection_variant &&connection) : conn(std::move(connection)) {}

    varlink_client(const varlink_client &src) = delete;
    varlink_client &operator=(const varlink_client &) = delete;
    varlink_client(varlink_client &&src) noexcept = default;
    varlink_client &operator=(varlink_client &&src) noexcept = default;

    std::function<json()> call(const std::string &method, const json &parameters, call_mode mode = call_mode::basic) {
        json message{{"method", method}};
        if (not parameters.is_null() and not parameters.is_object()) {
            throw std::invalid_argument("parameters is not an object");
        }
        if (not parameters.empty()) {
            message["parameters"] = parameters;
        }

        if (mode == call_mode::oneway) {
            message["oneway"] = true;
        } else if (mode == call_mode::more) {
            message["more"] = true;
        } else if (mode == call_mode::upgrade) {
            message["upgrade"] = true;
        }

        std::visit([&](auto &&c) { c.send(message); }, conn);

        return [this, mode, continues = true]() mutable -> json {
            if ((mode != call_mode::oneway) and continues) {
                json reply = std::visit([](auto &&c) { return c.receive(); }, conn);
                continues =
                    ((mode == call_mode::more) and reply.contains("continues") and reply["continues"].get<bool>());
                return reply;
            } else {
                return {};
            }
        };
    }
};
}  // namespace varlink

#endif
