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
using callmode = varlink_message::callmode;

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

    std::function<json()> call(const varlink_message &message) {
        std::visit([&](auto &&c) { c.send(message.json_data()); }, conn);

        return [this, continues = not message.oneway(), more = message.more()]() mutable -> json {
            if (continues) {
                json reply = std::visit([](auto &&c) { return c.receive(); }, conn);
                continues = (more and reply.contains("continues") and reply["continues"].get<bool>());
                return reply;
            } else {
                return {};
            }
        };
    }

    std::function<json()> call(const std::string &method, const json &parameters, callmode mode = callmode::basic) {
        return call(varlink_message(method, parameters, mode));
    }
};
}  // namespace varlink

#endif
