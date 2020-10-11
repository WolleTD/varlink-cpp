/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <string>
#include <utility>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename Socket, typename = std::enable_if_t<std::is_base_of_v<asio::socket_base, Socket> > >
class json_connection {
   public:
    using socket_type = Socket;
    using protocol_type = typename Socket::protocol_type;

   private:
    Socket stream;
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;

   public:
    explicit json_connection(Socket socket) : stream(std::move(socket)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    json_connection(const json_connection &) = delete;
    json_connection &operator=(const json_connection &) = delete;
    json_connection(json_connection &&) noexcept = default;
    json_connection &operator=(json_connection &&) noexcept = default;

    void send(const json &message) {
        const auto m = message.dump();
        const auto size = m.size() + 1;  // Include \0
        size_t sent = 0;
        while (sent < size) {
            sent += stream.send(asio::buffer(m.data() + sent, size - sent));
        }
    }

    [[nodiscard]] json receive() {
        if (read_end == readbuf.begin()) {
            const auto bytes_read = stream.receive(asio::buffer(readbuf));
            read_end = readbuf.begin() + static_cast<ptrdiff_t>(bytes_read);
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

using json_connection_unix = json_connection<asio::local::stream_protocol::socket>;
using json_connection_tcp = json_connection<asio::ip::tcp::socket>;

using endpoint_variant = std::variant<asio::local::stream_protocol::endpoint, asio::ip::tcp::endpoint>;

inline endpoint_variant endpoint_from_uri(const varlink_uri &uri) {
    if (uri.type == varlink_uri::type::unix) {
        return asio::local::stream_protocol::endpoint{uri.path};
    } else if (uri.type == varlink_uri::type::tcp) {
        uint16_t port{0};
        if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port); r.ptr != uri.port.end()) {
            throw std::invalid_argument("Invalid port");
        }
        return asio::ip::tcp::endpoint(asio::ip::make_address_v4(uri.host), port);
    } else {
        throw std::invalid_argument("Unsupported protocol");
    }
}
}  // namespace varlink

#endif
