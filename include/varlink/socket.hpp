/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SOCKET_HPP
#define LIBVARLINK_VARLINK_SOCKET_HPP

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <varlink/varlink.hpp>

namespace varlink::socket {
inline std::system_error systemErrorFromErrno(const std::string &what) {
    return {std::error_code(errno, std::system_category()), what};
}

namespace type {
template <int Family, int Type>
struct Base {
    static constexpr const int SOCK_FAMILY = Family;
    static constexpr const int SOCK_TYPE = Type;
};

struct Unix : Base<AF_UNIX, SOCK_STREAM>, ::sockaddr_un {
    constexpr Unix() : ::sockaddr_un{SOCK_FAMILY, ""} {}

    constexpr explicit Unix(std::string_view address) : Unix() {
        if (address.length() + 1 > sizeof(sun_path)) {
            throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
        }
        address.copy(sun_path, address.length());
    }
};
static_assert(sizeof(Unix) == sizeof(sockaddr_un));

struct TCP : Base<AF_INET, SOCK_STREAM>, ::sockaddr_in {
    constexpr TCP() : ::sockaddr_in{AF_INET, 0, {INADDR_ANY}, {}} {}

    constexpr explicit TCP(std::string_view host, uint16_t port) : TCP() {
        sin_port = htons(port);
        if (inet_pton(SOCK_FAMILY, host.data(), &sin_addr) < 1) {
            throw std::invalid_argument("Invalid address");
        }
    }
};
static_assert(sizeof(TCP) == sizeof(sockaddr_in));
}  // namespace type

enum class Mode { Connect, Listen, Raw };

template <typename SockaddrT, enum Mode Mode = Mode::Raw, int MaxConnections = 1024>
class PosixSocket {
   private:
    int socket_fd{-1};
    SockaddrT socketAddress{};

   protected:
    PosixSocket() = default;

    void socket(int type, int protocol) {
        if ((socket_fd = ::socket(SockaddrT::SOCK_FAMILY, type, protocol)) < 0) {
            throw systemErrorFromErrno("socket() failed");
        }
    }

   public:
    template <typename... Args>
    explicit PosixSocket(Args &&...args) : socketAddress(args...) {
        socket(SockaddrT::SOCK_TYPE | SOCK_CLOEXEC, 0);
        if constexpr (Mode == Mode::Connect) {
            connect();
        } else if constexpr (Mode == Mode::Listen) {
            bind();
            listen(MaxConnections);
        }
    }

    explicit PosixSocket(int fd) : socket_fd(fd) {}

    ~PosixSocket() {
        if constexpr (Mode == Mode::Listen) {
            shutdown(SHUT_RDWR);
            if constexpr (std::is_same_v<SockaddrT, type::Unix>) {
                unlink(socketAddress.sun_path);
            }
        }
        close(socket_fd);
    }

    void connect() {
        if (::connect(socket_fd, (sockaddr *)&socketAddress, sizeof(SockaddrT)) < 0) {
            throw systemErrorFromErrno("connect() failed");
        }
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        const auto ret = ::write(socket_fd, &(*begin), end - begin);
        if (ret < 0) {
            throw systemErrorFromErrno("write() failed");
        }
        return begin + ret;
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        const auto ret = ::read(socket_fd, &(*begin), end - begin);
        if (ret <= 0) {
            throw systemErrorFromErrno("read() failed");
        }
        return begin + ret;
    }

    void bind() {
        if (::bind(socket_fd, (sockaddr *)&socketAddress, sizeof(SockaddrT)) < 0) {
            throw systemErrorFromErrno("bind() failed");
        }
    }

    void listen(size_t max_connections) {  // NOLINT (is not const: socket changes)
        if (::listen(socket_fd, max_connections) < 0) {
            throw systemErrorFromErrno("listen() failed");
        }
    }

    int accept(SockaddrT *addr) {
        socklen_t addrlen = sizeof(SockaddrT);
        auto fd = ::accept(socket_fd, (sockaddr *)addr, &addrlen);
        if (fd < 0) {
            throw systemErrorFromErrno("accept() failed");
        }
        return fd;
    }

    void shutdown(int how) {  // NOLINT (is not const: socket changes)
        ::shutdown(socket_fd, how);
    }
};

template <enum Mode Mode, int MaxConnections = 1024>
using UnixSocket = PosixSocket<type::Unix, Mode, MaxConnections>;
template <enum Mode Mode, int MaxConnections = 1024>
using TCPSocket = PosixSocket<type::TCP, Mode, MaxConnections>;
}  // namespace varlink::socket

#endif
