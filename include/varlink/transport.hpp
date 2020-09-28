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

        template <typename IteratorT, typename = std::enable_if_t<
                std::is_convertible_v<typename IteratorT::value_type,char> > >
        IteratorT write(IteratorT begin, IteratorT end) {
            const auto ret = ::write(socket_fd, &(*begin), end - begin);
            if (ret < 0) {
                throw systemErrorFromErrno("write() failed");
            }
            return begin + ret;
        }

        template <typename IteratorT, typename = std::enable_if_t<
                std::is_convertible_v<typename IteratorT::value_type,char> > >
        IteratorT read(IteratorT begin, IteratorT end) {
            const auto ret = ::read(socket_fd, &(*begin), end - begin);
            if (ret <= 0) {
                throw systemErrorFromErrno("read() failed");
            }
            return begin + ret;
        }

        template <typename SockaddrT>
        void bind(SockaddrT &addr) {
            if (::bind(socket_fd, (struct sockaddr *)&addr, sizeof(SockaddrT)) < 0) {
                throw systemErrorFromErrno("bind() failed");
            }
        }

        void listen(size_t max_connections) { //NOLINT (is not const: socket changes)
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

        void shutdown(int how) { //NOLINT (is not const: socket changes)
            ::shutdown(socket_fd, how);
        }
    };

    template <typename SocketT>
    class JsonConnection {
    private:
        std::unique_ptr<SocketT> socket;
        using byte_buffer = std::vector<char>;
        byte_buffer readbuf;
        byte_buffer::iterator read_end;
    public:
        // Connect to a service via address
        explicit JsonConnection(const std::string &address) : socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0),
                                                              readbuf(BUFSIZ), read_end(readbuf.begin()) {
            struct sockaddr_un addr{AF_UNIX, ""};
            if (address.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            address.copy(addr.sun_path, address.length());
            socket.connect(addr);
        }

        // Setup message stream on existing connection
        explicit JsonConnection(int posix_fd) : socket(std::make_unique<SocketT>(posix_fd)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}
        explicit JsonConnection(std::unique_ptr<SocketT> existingSocket) : socket(std::move(existingSocket)),
                                                                           readbuf(BUFSIZ), read_end(readbuf.begin()) {}

        void send(const json &message) {
            const auto m = message.dump();
            const auto end = m.end() + 1; // Include \0
            auto sent = socket->write(m.begin(), end);
            while (sent < end) {
                sent = socket->write(sent, end);
            }
        }

        [[nodiscard]] json receive() {
            if (read_end == readbuf.begin()) {
                read_end = socket->read(readbuf.begin(), readbuf.end());
            }
            const auto next_message_end = std::find(readbuf.begin(), read_end, '\0');
            if (next_message_end == read_end) {
                throw std::runtime_error("Incomplete message received: " + std::string(readbuf.begin(), read_end));
            }
            const auto message = std::string(readbuf.begin(), next_message_end);
            read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());
            try {
                return json::parse(message);
            } catch (json::parse_error& e) {
                throw std::runtime_error("Json parse error: " + message);
            }
        }
    };

    template<typename SocketT>
    class ListeningConnection {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        SocketT socket;
    public:
        using ClientConnection = JsonConnection<SocketT>;

        explicit ListeningConnection(std::string address, const std::function<void()> &listener)
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

        ListeningConnection(const ListeningConnection &src) = delete;
        ListeningConnection &operator=(const ListeningConnection &) = delete;
        ListeningConnection(ListeningConnection &&src) = delete;
        ListeningConnection& operator=(ListeningConnection &&rhs) = delete;

        [[nodiscard]] ClientConnection nextClient() {
            if (auto client_fd = socket.accept((struct sockaddr_un *)nullptr); client_fd < 0) {
                throw systemErrorFromErrno("accept() failed");
            } else {
                return ClientConnection(client_fd);
            }
        }

        ~ListeningConnection() {
            socket.shutdown(SHUT_RDWR);
            listeningThread.join();
            unlink(socketAddress.c_str());
        }
    };

}

#endif
