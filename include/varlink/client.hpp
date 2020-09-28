/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <string>
#include <utility>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
    template<typename ConnectionT>
    class BasicClient {
    private:
        std::unique_ptr<ConnectionT> conn;
    public:
        enum class CallMode {
            Basic,
            Oneway,
            More,
            Upgrade,
        };

        explicit BasicClient(const std::string &address) : conn(std::make_unique<ConnectionT>(address)) {}

        explicit BasicClient(std::unique_ptr<ConnectionT> connection) : conn(std::move(connection)) {}

        std::function<json()> call(const std::string &method,
                                   const json &parameters,
                                   CallMode mode = CallMode::Basic) {
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
                    continues = ((mode == CallMode::More) and
                                 reply.contains("continues") and
                                 reply["continues"].get<bool>());
                    return reply;
                } else {
                    return {};
                }
            };
        }
    };

    using Client = BasicClient<JsonConnection<PosixSocket> >;
}

#endif
