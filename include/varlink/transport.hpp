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
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using result_type = json;

    executor_type get_executor() { return stream.get_executor(); }

   private:
    socket_type stream;
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;

   public:
    explicit json_connection(socket_type socket)
        : stream(std::move(socket)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

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

    template <ASIO_COMPLETION_TOKEN_FOR(void(asio::error_code))
                  CompletionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_send(const json &message, CompletionHandler &&handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type)) {
        return asio::async_initiate<CompletionHandler, void(asio::error_code)>(initiate_async_send(this), handler,
                                                                               message);
    }

    [[nodiscard]] json receive() {
        if (read_end == readbuf.begin()) {
            const auto bytes_read = stream.receive(asio::buffer(readbuf));
            read_end = readbuf.begin() + static_cast<ptrdiff_t>(bytes_read);
        }
        asio::error_code ec{};
        json j = read_next_message(ec);
        if (ec) {
            throw std::invalid_argument(j.get<std::string>() + std::string(readbuf.begin(), read_end));
        } else {
            return j;
        }
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(asio::error_code, json message))
                  CompletionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_receive(CompletionHandler &&handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type)) {
        return asio::async_initiate<CompletionHandler, void(asio::error_code, json)>(initiate_async_receive(this),
                                                                                     handler);
    }

   private:
    json read_next_message(asio::error_code &ec) {
        const auto next_message_end = std::find(readbuf.begin(), read_end, '\0');
        if (next_message_end == read_end) {
            ec = asio::error_code(asio::error::invalid_argument);
            return "Incomplete message received";
        }
        const auto message = std::string(readbuf.begin(), next_message_end);
        read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());

        try {
            return json::parse(message);
        } catch (json::parse_error &e) {
            ec = asio::error_code(asio::error::invalid_argument);
            return "Json parse error";
        }
    };

    class initiate_async_receive {
       private:
        json_connection<socket_type> *self_;

       public:
        explicit initiate_async_receive(json_connection<socket_type> *self) : self_(self) {}

        template <typename CompletionHandler>
        void operator()(CompletionHandler &&handler) {
            self_->stream.async_receive(asio::buffer(self_->readbuf),
                                        [self = self_, handler_ = std::forward<CompletionHandler>(handler)](
                                            asio::error_code ec, size_t n) mutable {
                                            if (ec) {
                                                handler_(ec, json{});
                                            } else {
                                                self->read_end = self->readbuf.begin() + static_cast<ptrdiff_t>(n);
                                                while (!ec) {
                                                    auto message = self->read_next_message(ec);
                                                    handler_(ec, message);
                                                }
                                            }
                                        });
        }
    };
    class initiate_async_send {
       private:
        json_connection<socket_type> *self_;

       public:
        explicit initiate_async_send(json_connection<socket_type> *self) : self_(self) {}

        template <typename CompletionHandler>
        void operator()(CompletionHandler &&handler, const json &message) {
            auto m = std::make_unique<std::string>(message.dump());
            const auto size = m->size() + 1;  // Include \0
            auto buf = asio::buffer(m->data(), size);
            asio::async_write(self_->stream, buf,
                              [handler = std::forward<CompletionHandler>(handler), m = std::move(m), size](
                                  asio::error_code ec, size_t) mutable { handler(ec); });
        }
    };
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
