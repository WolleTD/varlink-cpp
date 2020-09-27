/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <cerrno>
#include <ext/stdio_filebuf.h>
#include <fstream>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <iostream>
#include <varlink/varlink.hpp>

namespace varlink {
    inline std::system_error systemErrorFromErrno(const std::string &what) {
        return {std::error_code(errno, std::system_category()), what};
    }

    class StreamingConnection {
    private:
        std::istream rstream;
        std::ostream wstream;
    public:
        template<typename BufferT>
        StreamingConnection(BufferT *input, BufferT *output) : rstream(input), wstream(output) {}

        StreamingConnection(const StreamingConnection &src) = delete;

        StreamingConnection &operator=(const StreamingConnection &) = delete;

        StreamingConnection(StreamingConnection &&src) noexcept:
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
            struct sockaddr_un addr{AF_UNIX, ""};
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
    };

    class ListeningSocket {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        int listen_fd{-1};
    public:

        explicit ListeningSocket(std::string address, const std::function<void()> &listener)
                : socketAddress(std::move(address)) {
            if (not listener) {
                throw std::invalid_argument("Listening thread function required!");
            }
            listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (listen_fd < 0) {
                throw systemErrorFromErrno("socket() failed");
            }
            struct sockaddr_un addr{AF_UNIX, ""};
            if (socketAddress.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            socketAddress.copy(addr.sun_path, socketAddress.length());
            if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                throw systemErrorFromErrno("bind() failed");
            }
            if (::listen(listen_fd, 1024) < 0) {
                throw systemErrorFromErrno("listen() failed");
            }
            listeningThread = std::thread(listener);
        }

        ListeningSocket(const ListeningSocket &src) = delete;
        ListeningSocket &operator=(const ListeningSocket &) = delete;

        ListeningSocket(ListeningSocket &&src) noexcept :
                socketAddress(std::exchange(src.socketAddress, {})),
                listeningThread(std::exchange(src.listeningThread, {})),
                listen_fd(std::exchange(src.listen_fd, -1)) {}

        ListeningSocket& operator=(ListeningSocket &&rhs) noexcept {
            ListeningSocket s(std::move(rhs));
            std::swap(socketAddress, s.socketAddress);
            std::swap(listeningThread, s.listeningThread);
            std::swap(listen_fd, s.listen_fd);
            return *this;
        }

        [[nodiscard]] int nextClientFd() { //NOLINT (is not const: socket changes)
            auto client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd < 0) {
                throw systemErrorFromErrno("accept() failed");
            }
            return client_fd;
        }

        ~ListeningSocket() {
            shutdown(listen_fd, SHUT_RDWR);
            close(listen_fd);
            listeningThread.join();
            unlink(socketAddress.c_str());
        }
    };

}

#endif
