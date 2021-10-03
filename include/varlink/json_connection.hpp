/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <optional>
#include <varlink/detail/config.hpp>
#include <varlink/detail/manual_strand.hpp>
#include <varlink/detail/nl_json.hpp>

namespace varlink {

template <typename Protocol>
class json_connection {
  public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;

    executor_type get_executor() { return stream.get_executor(); }

  private:
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;
    socket_type stream;
    detail::manual_strand<executor_type> write_strand;

  public:
    explicit json_connection(asio::io_context& ctx) : json_connection(socket_type(ctx)) {}

    explicit json_connection(socket_type socket)
        : readbuf(BUFSIZ),
          read_end(readbuf.begin()),
          stream(std::move(socket)),
          write_strand(stream.get_executor())
    {
    }

    json_connection(const json_connection&) = delete;
    json_connection& operator=(const json_connection&) = delete;
    json_connection(json_connection&&) noexcept = default;
    // NOLINTNEXTLINE(performance-noexcept-move-constructor) stream's operator= isn't noexcept
    json_connection& operator=(json_connection&&) = default;

    template <typename ConnectHandler>
    decltype(auto) async_connect(const endpoint_type& endpoint, ConnectHandler&& handler)
    {
        return stream.async_connect(endpoint, std::forward<ConnectHandler>(handler));
    }

    void connect(const endpoint_type& endpoint) { stream.connect(endpoint); }
    void connect(const endpoint_type& endpoint, std::error_code& ec)
    {
        stream.connect(endpoint, ec);
    }

    void close() { stream.close(); }
    void close(std::error_code& ec) { stream.close(ec); }

    void cancel() { stream.cancel(); }
    void cancel(std::error_code& ec) { stream.cancel(ec); }

    bool is_open() { return stream.is_open(); }

    template <typename CompletionHandler>
    auto async_send(const json& message, CompletionHandler&& handler)
    {
        return net::async_initiate<CompletionHandler, void(std::error_code)>(
            initiate_async_send(this), handler, message);
    }

    template <typename CompletionHandler>
    auto async_receive(CompletionHandler&& handler)
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
        std::optional<json> j = read_next_message(ec);
        while (not j and not ec) {
            const auto bytes_read = stream.receive(
                net::buffer(&(*read_end), static_cast<size_t>(readbuf.end() - read_end)));
            read_end += static_cast<ptrdiff_t>(bytes_read);
            j = read_next_message(ec);
        }
        if (ec) { throw std::invalid_argument(std::string(readbuf.begin(), read_end)); }
        else {
            return j.value();
        }
    }

  private:
    std::optional<json> read_next_message(std::error_code& ec)
    {
        ec = std::error_code{};
        const auto next_message_end = std::find(readbuf.begin(), read_end, '\0');
        if (next_message_end == read_end) { return std::nullopt; }
        const auto message = std::string(readbuf.begin(), next_message_end);
        read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());

        try {
            return json::parse(message);
        }
        catch (json::parse_error&) {
            ec = net::error::invalid_argument;
            return json{};
        }
    };

    class initiate_async_receive {
      private:
        json_connection* self_;

      public:
        explicit initiate_async_receive(json_connection* self) : self_(self) {}

        template <typename CompletionHandler>
        void operator()(CompletionHandler&& handler)
        {
            std::error_code _ec;
            if (auto _message = self_->read_next_message(_ec); _message) {
                net::post(
                    self_->get_executor(),
                    [_ec, _message, handler = std::forward<CompletionHandler>(handler)]() mutable {
                        handler(_ec, std::move(_message.value()));
                    });
            }
            else {
                self_->stream.async_receive(
                    net::buffer(
                        &(*self_->read_end),
                        static_cast<size_t>(self_->readbuf.end() - self_->read_end)),
                    [self = self_, handler = std::forward<CompletionHandler>(handler)](
                        std::error_code ec, size_t n) mutable {
                        if (ec) { handler(ec, json{}); }
                        else {
                            self->read_end += static_cast<ptrdiff_t>(n);
                            if (auto message = self->read_next_message(ec); message) {
                                handler(ec, message.value());
                            }
                            else {
                                self->async_receive(std::forward<CompletionHandler>(handler));
                            }
                        }
                    });
            }
        }
    };
    class initiate_async_send {
      private:
        json_connection* self_;

      public:
        explicit initiate_async_send(json_connection* self) : self_(self) {}

        template <typename CompletionHandler>
        void operator()(CompletionHandler&& handler, const json& message)
        {
            self_->write_strand.push(
                [self = self_, &message, handler = std::forward<CompletionHandler>(handler)]() mutable {
                    auto m = std::make_unique<std::string>(message.dump());
                    auto buffer = net::buffer(m->data(), m->size() + 1);
                    net::async_write(
                        self->stream,
                        buffer,
                        [handler = std::forward<CompletionHandler>(handler), self, m = std::move(m)](
                            std::error_code ec, size_t) mutable {
                            self->write_strand.next();
                            handler(ec);
                        });
                });
        }
    };
};
} // namespace varlink

#endif
