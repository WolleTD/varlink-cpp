/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <cerrno>
#include <ext/stdio_filebuf.h>
#include <fstream>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>
#include <iostream>
#include <varlink/varlink.hpp>

namespace varlink {
    std::system_error systemErrorFromErrno(const std::string &what) {
        return {std::error_code(errno, std::system_category()), what};
    }

    class StreamingConnection {
    private:
        std::istream rstream;
        std::ostream wstream;
    public:
        template<typename BufferT>
        StreamingConnection(BufferT* input, BufferT* output) : rstream(input), wstream(output) {}

        StreamingConnection(const StreamingConnection &src) = delete;
        StreamingConnection &operator=(const StreamingConnection &) = delete;

        StreamingConnection(StreamingConnection &&src) noexcept :
        rstream(src.rstream.rdbuf()),
        wstream(src.wstream.rdbuf()) {
            src.rstream.rdbuf(nullptr);
            src.wstream.rdbuf(nullptr);
        }

        StreamingConnection &operator=(StreamingConnection &&rhs) noexcept {
            rstream.rdbuf(rhs.rstream.rdbuf());
            wstream.rdbuf(rhs.wstream.rdbuf());
            rhs.rstream.rdbuf(nullptr);
            rhs.wstream.rdbuf(nullptr);
            return *this;
        }

        void send(const json &message) {
            wstream << message << '\0' << std::flush;
            if (!wstream.good())
                throw systemErrorFromErrno("Writing to stream failed");
        }

        [[nodiscard]] json receive() {
            try {
                json message;
                rstream >> message;
                if (rstream.get() != '\0') {
                    throw std::invalid_argument("Trailing bytes in message");
                }
                return message;
            } catch (json::exception &e) {
                if (rstream.good()) {
                    throw std::invalid_argument(e.what());
                } else {
                    throw systemErrorFromErrno("Reading from stream failed");
                }
            }
        }
    };

    class SocketConnection : public StreamingConnection {
    private:
        int socket_fd{-1};
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;
    public:
        // Connect to a service via address
        explicit SocketConnection(const std::string &address) : StreamingConnection(&filebuf_in, &filebuf_out) {
            socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (socket_fd < 0) {
                throw systemErrorFromErrno("socket() failed");
            }
            struct sockaddr_un addr { AF_UNIX , "" };
            if (address.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            address.copy(addr.sun_path, address.length());
            if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                throw systemErrorFromErrno("connect() failed");
            }
            filebuf_in = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::in);
            filebuf_out = __gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::out);
        }

        // Setup message stream on existing connection
        explicit SocketConnection(int posix_fd) : StreamingConnection(&filebuf_in, &filebuf_out), socket_fd(posix_fd),
                filebuf_in(__gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::in)),
                filebuf_out(__gnu_cxx::stdio_filebuf<char>(socket_fd, std::ios::out)) {}

        SocketConnection(const SocketConnection &src) = delete;
        SocketConnection &operator=(const SocketConnection &) = delete;

        SocketConnection(SocketConnection &&src) noexcept :
                StreamingConnection(&filebuf_in, &filebuf_out),
                socket_fd(std::exchange(src.socket_fd, -1)),
                filebuf_in(std::move(src.filebuf_in)),
                filebuf_out(std::move(src.filebuf_out)) {}

        SocketConnection &operator=(SocketConnection &&rhs) noexcept {
            SocketConnection c(std::move(rhs));
            std::swap(socket_fd, c.socket_fd);
            std::swap(filebuf_in, c.filebuf_in);
            std::swap(filebuf_out, c.filebuf_out);
            return *this;
        }
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

    using Client = BasicClient<SocketConnection>;
}

#endif