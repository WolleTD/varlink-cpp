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

template <typename ConnectionT>
class BasicClient {
   private:
    std::unique_ptr<ConnectionT> conn;

   public:
    template <typename... Args>
    explicit BasicClient(Args &&...args)
        : conn(std::make_unique<ConnectionT>(socket::Mode::Connect, std::forward<Args>(args)...)) {}

    explicit BasicClient(std::unique_ptr<ConnectionT> connection) : conn(std::move(connection)) {}

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

        conn->send(message);

        return [this, mode, continues = true]() mutable -> json {
            if ((mode != CallMode::Oneway) and continues) {
                json reply = conn->receive();
                continues =
                    ((mode == CallMode::More) and reply.contains("continues") and reply["continues"].get<bool>());
                return reply;
            } else {
                return {};
            }
        };
    }
};

using UnixClient = BasicClient<JsonConnection<socket::UnixSocket> >;
using TCPClient = BasicClient<JsonConnection<socket::TCPSocket> >;

class VarlinkClient {
   public:
    using ClientT = std::variant<TCPClient, UnixClient>;

   private:
    ClientT client;

    static ClientT makeClient(const VarlinkURI &uri) {
        if (uri.type == VarlinkURI::Type::Unix) {
            return UnixClient{uri.path};
        } else if (uri.type == VarlinkURI::Type::TCP) {
            uint16_t port;
            if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port); r.ptr != uri.port.end()) {
                throw std::invalid_argument("Invalid port");
            }
            return TCPClient(uri.host, port);
        } else {
            throw std::invalid_argument("Unsupported protocol");
        }
    }

   public:
    explicit VarlinkClient(std::string_view uri) : client(makeClient(VarlinkURI(uri))) {}

    std::function<json()> call(const std::string &method, const json &parameters, CallMode mode = CallMode::Basic) {
        return std::visit([&](auto &&cl) { return cl.call(method, parameters, mode); }, client);
    }
};
}  // namespace varlink

#endif
