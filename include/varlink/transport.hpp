/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <string>
#include <utility>
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <varlink/detail/manual_strand.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
namespace net = ::asio;
static_assert(std::is_same_v<net::error_code, std::error_code>);

template <
    typename Socket,
    typename = std::enable_if_t<std::is_base_of_v<net::socket_base, Socket>>>
class json_connection {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using result_type = json;

    socket_type& socket() { return stream; }
    const socket_type& socket() const { return stream; }

    executor_type get_executor() { return stream.get_executor(); }

  private:
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;
    socket_type stream;
    net::strand<executor_type> read_strand;
    detail::manual_strand<executor_type> write_strand;

  public:
    explicit json_connection(socket_type socket)
        : readbuf(BUFSIZ),
          read_end(readbuf.begin()),
          stream(std::move(socket)),
          read_strand(stream.get_executor()),
          write_strand(stream.get_executor())
    {
    }

    json_connection(const json_connection&) = delete;
    json_connection& operator=(const json_connection&) = delete;
    json_connection(json_connection&&) noexcept = default;
    json_connection& operator=(json_connection&&) noexcept = default;

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::error_code))
                  CompletionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_send(
        const json& message,
        CompletionHandler&& handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return net::async_initiate<CompletionHandler, void(std::error_code)>(
            initiate_async_send(this), handler, message);
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::error_code, json message))
                  CompletionHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_receive(
        CompletionHandler&& handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        return net::async_initiate<CompletionHandler, void(std::error_code, json)>(
            initiate_async_receive(this), handler);
    }

    void send(const json& message)
    {
        const auto m = message.dump();
        const auto size = m.size() + 1; // Include \0
        size_t sent = 0;
        while (sent < size) {
            sent += stream.send(net::buffer(m.data() + sent, size - sent));
        }
    }

    [[nodiscard]] json receive()
    {
        std::error_code ec{};
        json j = read_next_message(ec);
        while (ec == net::error::in_progress) {
            const auto bytes_read = stream.receive(net::buffer(
                &(*read_end), static_cast<size_t>(readbuf.end() - read_end)));
            read_end += static_cast<ptrdiff_t>(bytes_read);
            j = read_next_message(ec);
        }
        if (ec and ec != net::error::try_again) {
            throw std::invalid_argument(std::string(readbuf.begin(), read_end));
        }
        else {
            return j;
        }
    }

  private:
    json read_next_message(std::error_code& ec)
    {
        ec = std::error_code{};
        const auto next_message_end = std::find(readbuf.begin(), read_end, '\0');
        if (next_message_end == read_end) {
            ec = net::error::in_progress;
            return {};
        }
        const auto message = std::string(readbuf.begin(), next_message_end);
        read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());
        if (read_end != readbuf.begin()) {
            ec = net::error::try_again;
        }

        try {
            return json::parse(message);
        }
        catch (json::parse_error& e) {
            ec = net::error::invalid_argument;
            return {};
        }
    };

    class initiate_async_receive {
      private:
        json_connection<socket_type>* self_;

      public:
        explicit initiate_async_receive(json_connection<socket_type>* self)
            : self_(self)
        {
        }

        template <typename CompletionHandler>
        void operator()(CompletionHandler&& handler)
        {
            self_->stream.async_receive(
                net::buffer(
                    &(*self_->read_end),
                    static_cast<size_t>(self_->readbuf.end() - self_->read_end)),
                net::bind_executor(
                    self_->read_strand,
                    [self = self_,
                     handler = std::forward<CompletionHandler>(handler)](
                        std::error_code ec, size_t n) mutable {
                        if (ec) {
                            handler(ec, json{});
                        }
                        else {
                            self->read_end += static_cast<ptrdiff_t>(n);
                            while (not ec or ec == net::error::try_again) {
                                auto message = self->read_next_message(ec);
                                if (ec != net::error::in_progress) {
                                    handler(ec, message);
                                }
                                else if (self->read_end != self->readbuf.begin()) {
                                    self->async_receive(handler);
                                }
                            }
                        }
                    }));
        }
    };
    class initiate_async_send {
      private:
        json_connection<socket_type>* self_;

      public:
        explicit initiate_async_send(json_connection<socket_type>* self)
            : self_(self)
        {
        }

        template <typename CompletionHandler>
        void operator()(CompletionHandler&& handler, const json& message)
        {
            self_->write_strand.push(
                [self = self_,
                 &message,
                 handler = std::forward<CompletionHandler>(handler)]() mutable {
                    auto m = std::make_unique<std::string>(message.dump());
                    auto buffer = net::buffer(m->data(), m->size() + 1);
                    net::async_write(
                        self->stream,
                        buffer,
                        [handler = std::forward<CompletionHandler>(handler),
                         self,
                         m = std::move(m)](std::error_code ec, size_t) mutable {
                            self->write_strand.next();
                            handler(ec);
                        });
                });
        }
    };
};

using json_connection_unix = json_connection<net::local::stream_protocol::socket>;
using json_connection_tcp = json_connection<net::ip::tcp::socket>;

using endpoint_variant =
    std::variant<net::local::stream_protocol::endpoint, net::ip::tcp::endpoint>;

inline endpoint_variant endpoint_from_uri(const varlink_uri& uri)
{
    if (uri.type == varlink_uri::type::unix) {
        return net::local::stream_protocol::endpoint{uri.path};
    }
    else if (uri.type == varlink_uri::type::tcp) {
        uint16_t port{0};
        if (auto r = std::from_chars(uri.port.begin(), uri.port.end(), port);
            r.ptr != uri.port.end()) {
            throw std::invalid_argument("Invalid port");
        }
        return net::ip::tcp::endpoint(net::ip::make_address_v4(uri.host), port);
    }
    else {
        throw std::invalid_argument("Unsupported protocol");
    }
}
} // namespace varlink

#endif
