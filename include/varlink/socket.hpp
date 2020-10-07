/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SOCKET_HPP
#define LIBVARLINK_VARLINK_SOCKET_HPP

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <varlink/varlink.hpp>
#undef unix

namespace varlink::socket {
inline std::system_error system_error_from_errno(const std::string &what) {
    if (errno == 0) {
        return {std::make_error_code(std::errc{}), what};
    } else {
        return {std::error_code(errno, std::system_category()), what};
    }
}

namespace type {
template <int Family, int Type>
struct base {
    static constexpr const int SOCK_FAMILY = Family;
    static constexpr const int SOCK_TYPE = Type;
};

struct unspec : base<AF_UNSPEC, 0> {};

struct unix : base<AF_UNIX, SOCK_STREAM>, ::sockaddr_un {
    unix() : ::sockaddr_un{SOCK_FAMILY, ""} {}

    explicit unix(std::string_view address) : unix() {
        if (address.length() + 1 > sizeof(sun_path)) {
            throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
        }
        address.copy(sun_path, address.length());
    }
};
static_assert(sizeof(unix) == sizeof(sockaddr_un));

struct tcp : base<AF_INET, SOCK_STREAM>, ::sockaddr_in {
    tcp() : ::sockaddr_in{AF_INET, 0, {INADDR_ANY}, {}} {}

    explicit tcp(std::string_view host, uint16_t port) : tcp() {
        auto host_str = std::string(host);
        sin_port = htons(port);
        if (auto r = inet_pton(SOCK_FAMILY, host_str.c_str(), &sin_addr); r < 1) {
            throw std::invalid_argument("Invalid address " + std::string(host) + std::to_string(r));
        }
    }
};
static_assert(sizeof(tcp) == sizeof(sockaddr_in));

using variant = std::variant<socket::type::unix, socket::type::tcp>;

inline variant from_uri(const varlink_uri &uri) {
    if (uri.type == varlink_uri::type::unix) {
        return socket::type::unix{uri.path};
    } else if (uri.type == varlink_uri::type::tcp) {
        uint16_t port{0};
        if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port); r.ptr != uri.port.end()) {
            throw std::invalid_argument("Invalid port");
        }
        return socket::type::tcp(uri.host, port);
    } else {
        throw std::invalid_argument("Unsupported protocol");
    }
}
}  // namespace type

enum class mode { connect, listen, raw };

template <typename SockaddrT, int MaxConnections = 1024>
class basic_socket {
   private:
    int socket_fd{-1};
    SockaddrT socket_address{};
    mode socket_mode{mode::raw};

   protected:
    basic_socket() = default;

    void socket(int type, int protocol) {
        if ((socket_fd = ::socket(SockaddrT::SOCK_FAMILY, type, protocol)) < 0) {
            throw system_error_from_errno("socket() failed");
        }
    }

   public:
    template <typename... Args>
    explicit basic_socket(mode mode, Args &&...args) : socket_address(args...), socket_mode(mode) {
        socket(SockaddrT::SOCK_TYPE | SOCK_CLOEXEC, 0);
        try {
            if (mode == mode::connect) {
                connect();
            } else if (mode == mode::listen) {
                if constexpr (std::is_same_v<SockaddrT, type::tcp>) {
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

    explicit basic_socket(int fd) : socket_fd(fd) {}

    basic_socket(const basic_socket &src) = delete;
    basic_socket &operator=(const basic_socket &rhs) = delete;
    basic_socket(basic_socket &&src) noexcept
        : socket_fd(std::exchange(src.socket_fd, -1)),
          socket_address(std::exchange(src.socket_address, {})),
          socket_mode(src.socket_mode) {}

    basic_socket &operator=(basic_socket &&rhs) noexcept {
        basic_socket s(std::move(rhs));
        std::swap(socket_fd, s.socket_fd);
        std::swap(socket_address, s.socket_address);
        std::swap(socket_mode, s.socket_mode);
        return *this;
    }

    ~basic_socket() {
        shutdown(SHUT_RDWR);
        close(socket_fd);
        if constexpr (std::is_same_v<SockaddrT, type::unix>) {
            if (socket_mode == mode::listen) {
                unlink(socket_address.sun_path);
            }
        }
    }

    int remove_fd() noexcept { return std::exchange(socket_fd, -1); }
    template <typename SocketT, typename>
    friend int remove_fd(SocketT socket) noexcept;

    SockaddrT get_sockaddr() const noexcept { return socket_address; }

    void connect() {
        if (::connect(socket_fd, reinterpret_cast<sockaddr *>(&socket_address), sizeof(SockaddrT)) < 0) {
            throw system_error_from_errno("connect() failed");
        }
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        const auto ret = ::write(socket_fd, &(*begin), static_cast<size_t>(end - begin));
        if (ret < 0) {
            throw system_error_from_errno("write() failed");
        }
        return begin + ret;
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        const auto ret = ::read(socket_fd, &(*begin), static_cast<size_t>(end - begin));
        if (ret <= 0) {
            throw system_error_from_errno("read() failed");
        }
        return begin + ret;
    }

    void bind() {
        if (::bind(socket_fd, reinterpret_cast<sockaddr *>(&socket_address), sizeof(SockaddrT)) < 0) {
            throw system_error_from_errno("bind() failed");
        }
    }

    void listen(int max_connections) {  // NOLINT (is not const: socket changes)
        if (::listen(socket_fd, max_connections) < 0) {
            throw system_error_from_errno("listen() failed");
        }
    }

    int accept(SockaddrT *addr) {
        socklen_t addrlen = sizeof(SockaddrT);
        auto fd = ::accept(socket_fd, reinterpret_cast<sockaddr *>(addr), &addrlen);
        if (fd < 0) {
            throw system_error_from_errno("accept() failed");
        }
        return fd;
    }

    void shutdown(int how) {  // NOLINT (is not const: socket changes)
        ::shutdown(socket_fd, how);
    }

    template <typename T>
    void setsockopt(int level, int option_name, const T &option_value) {
        if (::setsockopt(socket_fd, level, option_name, &option_value, sizeof(T)) < 0) {
            throw system_error_from_errno("setsockopt() failed");
        }
    }
};

template <typename SocketT, typename = std::enable_if<std::is_rvalue_reference_v<SocketT> > >
inline int remove_fd(SocketT socket) noexcept {
    return std::exchange(socket.socket_fd, -1);
}

using unix = basic_socket<type::unix>;
using tcp = basic_socket<type::tcp>;
using variant = std::variant<socket::unix, socket::tcp>;
}  // namespace varlink::socket

#endif
