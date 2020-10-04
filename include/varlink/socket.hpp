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

struct Unspecified : Base<AF_UNSPEC, 0> {};

struct Unix : Base<AF_UNIX, SOCK_STREAM>, ::sockaddr_un {
    Unix() : ::sockaddr_un{SOCK_FAMILY, ""} {}

    explicit Unix(std::string_view address) : Unix() {
        if (address.length() + 1 > sizeof(sun_path)) {
            throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
        }
        address.copy(sun_path, address.length());
    }
};
static_assert(sizeof(Unix) == sizeof(sockaddr_un));

struct TCP : Base<AF_INET, SOCK_STREAM>, ::sockaddr_in {
    TCP() : ::sockaddr_in{AF_INET, 0, {INADDR_ANY}, {}} {}

    explicit TCP(std::string_view host, uint16_t port) : TCP() {
        auto host_str = std::string(host);
        sin_port = htons(port);
        if (auto r = inet_pton(SOCK_FAMILY, host_str.c_str(), &sin_addr); r < 1) {
            throw std::invalid_argument("Invalid address " + std::string(host) + std::to_string(r));
        }
    }
};
static_assert(sizeof(TCP) == sizeof(sockaddr_in));
}  // namespace type

enum class Mode { Connect, Listen, Raw };

template <typename SockaddrT, int MaxConnections = 1024>
class PosixSocket {
   private:
    int socket_fd{-1};
    SockaddrT socketAddress{};
    Mode socket_mode{Mode::Raw};

   protected:
    PosixSocket() = default;

    void socket(int type, int protocol) {
        if ((socket_fd = ::socket(SockaddrT::SOCK_FAMILY, type, protocol)) < 0) {
            throw systemErrorFromErrno("socket() failed");
        }
    }

   public:
    template <typename... Args>
    explicit PosixSocket(Mode mode, Args &&...args) : socketAddress(args...), socket_mode(mode) {
        socket(SockaddrT::SOCK_TYPE | SOCK_CLOEXEC, 0);
        try {
            if (mode == Mode::Connect) {
                connect();
            } else if (mode == Mode::Listen) {
                if constexpr (std::is_same_v<SockaddrT, type::TCP>) {
                    setsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
                }
                bind();
                listen(MaxConnections);
            }
        } catch (std::system_error &e) {
            close(socket_fd);
            throw;
        }
    }

    explicit PosixSocket(int fd) : socket_fd(fd) {}

    PosixSocket(const PosixSocket &src) = delete;
    PosixSocket &operator=(const PosixSocket &rhs) = delete;
    PosixSocket(PosixSocket &&src) noexcept
        : socket_fd(std::exchange(src.socket_fd, -1)),
          socketAddress(std::exchange(src.socketAddress, {})),
          socket_mode(src.socket_mode) {}

    PosixSocket &operator=(PosixSocket &&rhs) noexcept {
        PosixSocket s(std::move(rhs));
        std::swap(socket_fd, s.socket_fd);
        std::swap(socketAddress, s.socketAddress);
        std::swap(socket_mode, s.socket_mode);
        return *this;
    }

    ~PosixSocket() {
        shutdown(SHUT_RDWR);
        close(socket_fd);
        if constexpr (std::is_same_v<SockaddrT, type::Unix>) {
            if (socket_mode == Mode::Listen) {
                unlink(socketAddress.sun_path);
            }
        }
    }

    int remove_fd() noexcept { return std::exchange(socket_fd, -1); }
    template <typename SocketT, typename>
    friend int remove_fd(SocketT socket) noexcept;

    SockaddrT get_sockaddr() const noexcept { return socketAddress; }

    void connect() {
        if (::connect(socket_fd, reinterpret_cast<sockaddr *>(&socketAddress), sizeof(SockaddrT)) < 0) {
            throw systemErrorFromErrno("connect() failed");
        }
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        const auto ret = ::write(socket_fd, &(*begin), static_cast<size_t>(end - begin));
        if (ret < 0) {
            throw systemErrorFromErrno("write() failed");
        }
        return begin + ret;
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        const auto ret = ::read(socket_fd, &(*begin), static_cast<size_t>(end - begin));
        if (ret <= 0) {
            throw systemErrorFromErrno("read() failed");
        }
        return begin + ret;
    }

    void bind() {
        if (::bind(socket_fd, reinterpret_cast<sockaddr *>(&socketAddress), sizeof(SockaddrT)) < 0) {
            throw systemErrorFromErrno("bind() failed");
        }
    }

    void listen(int max_connections) {  // NOLINT (is not const: socket changes)
        if (::listen(socket_fd, max_connections) < 0) {
            throw systemErrorFromErrno("listen() failed");
        }
    }

    int accept(SockaddrT *addr) {
        socklen_t addrlen = sizeof(SockaddrT);
        auto fd = ::accept(socket_fd, reinterpret_cast<sockaddr *>(addr), &addrlen);
        if (fd < 0) {
            throw systemErrorFromErrno("accept() failed");
        }
        return fd;
    }

    void shutdown(int how) {  // NOLINT (is not const: socket changes)
        ::shutdown(socket_fd, how);
    }

    template <typename T>
    void setsockopt(int level, int option_name, const T &option_value) {
        if (::setsockopt(socket_fd, level, option_name, &option_value, sizeof(T)) < 0) {
            throw systemErrorFromErrno("setsockopt() failed");
        }
    }
};

template <typename SocketT, typename = std::enable_if<std::is_rvalue_reference_v<SocketT> > >
inline int remove_fd(SocketT socket) noexcept {
    return std::exchange(socket.socket_fd, -1);
}

using UnixSocket = PosixSocket<type::Unix>;
using TCPSocket = PosixSocket<type::TCP>;
}  // namespace varlink::socket

#endif
