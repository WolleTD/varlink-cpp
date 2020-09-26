/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <ext/stdio_filebuf.h>
#include <iostream>
#include <varlink/varlink.hpp>

namespace varlink {
    class Connection {
    private:
        int socket_fd{-1};
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;

        std::istream rstream{&filebuf_in};
        std::ostream wstream{&filebuf_out};
    public:
        // Connect to a service via address
        explicit Connection(const std::string &address);

        // Setup message stream on existing connection
        explicit Connection(int posix_fd);

        Connection(const Connection &src) = delete;

        Connection &operator=(const Connection &) = delete;

        Connection(Connection &&src) noexcept;

        Connection &operator=(Connection &&rhs) noexcept;

        void send(const json &message);

        [[nodiscard]] json receive();
    };

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
            if (!parameters.is_null() && !parameters.is_object()) {
                throw std::invalid_argument("parameters is not an object");
            }
            if (!parameters.empty()) {
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
                if (mode != CallMode::Oneway && continues) {
                    json reply = conn->receive();
                    if (mode == CallMode::More && reply.contains("continues")) {
                        continues = reply["continues"].get<bool>();
                    } else {
                        continues = false;
                    }
                    return reply;
                } else {
                    return {};
                }
            };
        }
    };
}

#endif