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

namespace varlink {
inline std::system_error systemErrorFromErrno(const std::string &what) {
    return {std::error_code(errno, std::system_category()), what};
}

struct Unix {
    static constexpr const int SOCK_FAMILY = AF_UNIX;
    static constexpr const int SOCK_TYPE = SOCK_STREAM;

    struct sockaddr : ::sockaddr_un {
        constexpr sockaddr() : ::sockaddr_un{AF_UNIX, ""} {}

        constexpr explicit sockaddr(std::string_view address) : sockaddr() {
            if (address.length() + 1 > sizeof(sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            address.copy(sun_path, address.length());
        }
    };
};

struct TCP {
    static constexpr const int SOCK_FAMILY = AF_INET;
    static constexpr const int SOCK_TYPE = SOCK_STREAM;

    struct sockaddr : ::sockaddr_in {
        constexpr sockaddr() : ::sockaddr_in{AF_INET, 0, {INADDR_ANY}, {}} {}

        constexpr explicit sockaddr(std::string_view host, uint16_t port) : sockaddr() {
            sin_port = htons(port);
            if (inet_pton(SOCK_FAMILY, host.data(), &sin_addr) < 0) {
                throw std::invalid_argument("Invalid address");
            }
        }
    };
};

template <typename FamilyT>
class PosixSocket {
   public:
    using SockaddrT = typename FamilyT::sockaddr;

   private:
    int socket_fd{-1};
    SockaddrT socketAddress{};

   protected:
    PosixSocket() = default;

    void socket(int type, int protocol) {
        if ((socket_fd = ::socket(FamilyT::SOCK_FAMILY, type, protocol)) < 0) {
            throw systemErrorFromErrno("socket() failed");
        }
    }

   public:
    template <typename... Args>
    explicit PosixSocket(Args &&...args) : socketAddress(args...) {
        socket(FamilyT::SOCK_TYPE | SOCK_CLOEXEC, 0);
    }

    explicit PosixSocket(int fd) : socket_fd(fd) {}

    ~PosixSocket() { close(socket_fd); }

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

    void listen(size_t max_connections) {  // NOLINT (is not const: socket
                                           // changes)
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
}  // namespace varlink

#endif
