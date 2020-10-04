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
enum class CallMode {
    Basic,
    Oneway,
    More,
    Upgrade,
};

class VarlinkClient {
   public:
    using ConnectionT = std::variant<json_connection_unix, json_connection_tcp>;

   private:
    ConnectionT conn;

   public:
    explicit VarlinkClient(const VarlinkURI &uri) : conn(make_connection(uri)) {}
    explicit VarlinkClient(std::string_view uri) : VarlinkClient(VarlinkURI(uri)) {}
    explicit VarlinkClient(ConnectionT &&connection) : conn(std::move(connection)) {}

    VarlinkClient(const VarlinkClient &src) = delete;
    VarlinkClient &operator=(const VarlinkClient &) = delete;
    VarlinkClient(VarlinkClient &&src) noexcept = default;
    VarlinkClient &operator=(VarlinkClient &&src) noexcept = default;

    std::function<json()> call(const std::string &method, const json &parameters, CallMode mode = CallMode::Basic) {
        json message{{"method", method}};
        if (not parameters.is_null() and not parameters.is_object()) {
            throw std::invalid_argument("parameters is not an object");
        }
        if (not parameters.empty()) {
            message["parameters"] = parameters;
        }

        if (mode == CallMode::Oneway) {
            message["oneway"] = true;
        } else if (mode == CallMode::More) {
            message["more"] = true;
        } else if (mode == CallMode::Upgrade) {
            message["upgrade"] = true;
        }

        std::visit([&](auto &&c) { c.send(message); }, conn);

        return [this, mode, continues = true]() mutable -> json {
            if ((mode != CallMode::Oneway) and continues) {
                json reply = std::visit([](auto &&c) { return c.receive(); }, conn);
                continues =
                    ((mode == CallMode::More) and reply.contains("continues") and reply["continues"].get<bool>());
                return reply;
            } else {
                return {};
            }
        };
    }
};
}  // namespace varlink

#endif
