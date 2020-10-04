/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <string>
#include <utility>
#include <varlink/socket.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename SockaddrT, typename SocketT = socket::basic_socket<SockaddrT> >
class basic_json_connection {
   private:
    std::unique_ptr<SocketT> socket;
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;

   public:
    template <typename... Args>
    explicit basic_json_connection(Args &&...args)
        : socket(std::make_unique<SocketT>(socket::mode::connect, args...)),
          readbuf(BUFSIZ),
          read_end(readbuf.begin()) {}

    // Setup message stream on existing connection
    explicit basic_json_connection(int posix_fd)
        : socket(std::make_unique<SocketT>(posix_fd)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    explicit basic_json_connection(std::unique_ptr<SocketT> existingSocket)
        : socket(std::move(existingSocket)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    basic_json_connection(const basic_json_connection &) = delete;
    basic_json_connection &operator=(const basic_json_connection &) = delete;
    basic_json_connection(basic_json_connection &&) noexcept = default;
    basic_json_connection &operator=(basic_json_connection &&) noexcept = default;

    void send(const json &message) {
        const auto m = message.dump();
        const auto end = m.end() + 1;  // Include \0
        auto sent = m.begin();
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
            throw std::invalid_argument("Incomplete message received: " + std::string(readbuf.begin(), read_end));
        }
        const auto message = std::string(readbuf.begin(), next_message_end);
        read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());
        try {
            return json::parse(message);
        } catch (json::parse_error &e) {
            throw std::invalid_argument("Json parse error: " + message);
        }
    }
};

using json_connection_unix = basic_json_connection<socket::type::unix>;
using json_connection_tcp = basic_json_connection<socket::type::tcp>;
using json_connection_variant = std::variant<json_connection_unix, json_connection_tcp>;

template <template <typename> typename Target,
          typename ResultT = std::variant<Target<socket::type::unix>, Target<socket::type::tcp> >, typename... Args>
ResultT make_from_uri(const varlink_uri &uri, Args &&...args) {
    return std::visit(
        [&](auto &&sockaddr) -> ResultT {
            using T = std::decay_t<decltype(sockaddr)>;
            return Target<T>(args..., sockaddr);
        },
        socket::type::from_uri(uri));
}
}  // namespace varlink

#endif
