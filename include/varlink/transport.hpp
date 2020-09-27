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

    class PosixSocket {
    private:
        int socket_fd{-1};
    public:
        PosixSocket(int domain, int type, int protocol) {
            if ((socket_fd = socket(domain, type, protocol)) < 0) {
                throw systemErrorFromErrno("socket() failed");
            }
        }

        explicit PosixSocket(int fd) : socket_fd(fd) {}

        ~PosixSocket() {
            close(socket_fd);
        }

        template <typename SockaddrT>
        void connect(SockaddrT &addr) {
            if (::connect(socket_fd, (struct sockaddr *)&addr, sizeof(SockaddrT)) < 0) {
                throw systemErrorFromErrno("connect() failed");
            }
        }

        template <typename SockaddrT>
        void bind(SockaddrT &addr) {
            if (::bind(socket_fd, (struct sockaddr *)&addr, sizeof(SockaddrT)) < 0) {
                throw systemErrorFromErrno("bind() failed");
            }
        }

        void listen(size_t max_connections) {
            if (::listen(socket_fd, max_connections) < 0) {
                throw systemErrorFromErrno("listen() failed");
            }
        }

        template <typename SockaddrT>
        int accept(SockaddrT *addr) {
            socklen_t addrlen = sizeof(SockaddrT);
            auto fd = ::accept(socket_fd, (struct sockaddr *)addr, &addrlen);
            if (fd < 0) {
                throw systemErrorFromErrno("accept() failed");
            }
            return fd;
        }

        void shutdown(int how) {
            ::shutdown(socket_fd, how);
        }

        [[nodiscard]] int fd() const { return socket_fd; }
    };

    class JsonConnection {
    private:
        std::istream rstream;
        std::ostream wstream;
    public:
        template<typename BufferT>
        JsonConnection(BufferT *input, BufferT *output) : rstream(input), wstream(output) {}

        JsonConnection(const JsonConnection &src) = delete;

        JsonConnection &operator=(const JsonConnection &) = delete;

        JsonConnection(JsonConnection &&src) noexcept:
                rstream(src.rstream.rdbuf()),
                wstream(src.wstream.rdbuf()) {
            src.rstream.rdbuf(nullptr);
            src.wstream.rdbuf(nullptr);
        }

        JsonConnection &operator=(JsonConnection &&rhs) noexcept {
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

    class SocketConnection : public JsonConnection {
    private:
        PosixSocket socket;
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;
    public:
        // Connect to a service via address
        explicit SocketConnection(const std::string &address) : JsonConnection(&filebuf_in, &filebuf_out),
                                                                socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) {
            struct sockaddr_un addr{AF_UNIX, ""};
            if (address.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            address.copy(addr.sun_path, address.length());
            socket.connect(addr);

            filebuf_in = __gnu_cxx::stdio_filebuf<char>(socket.fd(), std::ios::in);
            filebuf_out = __gnu_cxx::stdio_filebuf<char>(socket.fd(), std::ios::out);
        }

        // Setup message stream on existing connection
        explicit SocketConnection(int posix_fd) : JsonConnection(&filebuf_in, &filebuf_out), socket(posix_fd),
                                                  filebuf_in(__gnu_cxx::stdio_filebuf<char>(socket.fd(), std::ios::in)),
                                                  filebuf_out(__gnu_cxx::stdio_filebuf<char>(socket.fd(), std::ios::out)) {}
    };

    class ListeningSocket {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        PosixSocket socket;
    public:

        explicit ListeningSocket(std::string address, const std::function<void()> &listener)
                : socketAddress(std::move(address)), socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0) {
            if (not listener) {
                throw std::invalid_argument("Listening thread function required!");
            }
            struct sockaddr_un addr{AF_UNIX, ""};
            if (socketAddress.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            socketAddress.copy(addr.sun_path, socketAddress.length());
            socket.bind(addr);
            socket.listen(1024);
            listeningThread = std::thread(listener);
        }

        ListeningSocket(const ListeningSocket &src) = delete;
        ListeningSocket &operator=(const ListeningSocket &) = delete;

        ListeningSocket(ListeningSocket &&src) noexcept :
                socketAddress(std::exchange(src.socketAddress, {})),
                listeningThread(std::exchange(src.listeningThread, {})),
                socket(std::exchange(src.socket, PosixSocket(-1))) {}

        ListeningSocket& operator=(ListeningSocket &&rhs) noexcept {
            ListeningSocket s(std::move(rhs));
            std::swap(socketAddress, s.socketAddress);
            std::swap(listeningThread, s.listeningThread);
            std::swap(socket, s.socket);
            return *this;
        }

        [[nodiscard]] int nextClientFd() { //NOLINT (is not const: socket changes)
            auto client_fd = socket.accept((struct sockaddr_un *)nullptr);
            if (client_fd < 0) {
                throw systemErrorFromErrno("accept() failed");
            }
            return client_fd;
        }

        ~ListeningSocket() {
            socket.shutdown(SHUT_RDWR);
            listeningThread.join();
            unlink(socketAddress.c_str());
        }
    };

}

#endif
