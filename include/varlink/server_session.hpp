#ifndef LIBVARLINK_SERVER_SESSION_HPP
#define LIBVARLINK_SERVER_SESSION_HPP

#include <varlink/json_connection.hpp>
#include <varlink/service.hpp>

namespace varlink {
template <typename Protocol>
struct server_session : std::enable_shared_from_this<server_session<Protocol>> {
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<protocol_type>;

    using std::enable_shared_from_this<server_session>::shared_from_this;

    explicit server_session(socket_type socket, varlink_service& service)
        : connection(std::move(socket)), service_(service)
    {
    }

    server_session(const server_session&) = delete;
    server_session& operator=(const server_session&) = delete;
    server_session(server_session&&) noexcept = default;
    server_session& operator=(server_session&&) noexcept = default;

    executor_type get_executor() { return connection.get_executor(); }

    void start()
    {
        connection.async_receive([self = shared_from_this()](auto ec, auto&& j) {
            if (ec) return;
            try {
                const basic_varlink_message message{j};
                self->service_.message_call(
                    message, [self](const json& reply, more_handler&& moreHandler) {
                        auto continues = bool(moreHandler);
                        if (reply.is_object()) {
                            auto m = std::make_unique<json>(reply);
                            self->async_send_reply(std::move(m), moreHandler);
                        }
                        if (not continues) { self->start(); }
                    });
            }
            catch (...) {
            }
        });
    }

  private:
    template <typename MoreHandler>
    void async_send_reply(std::unique_ptr<json> message, MoreHandler&& moreHandler)
    {
        auto& data = *message;
        connection.async_send(
            data,
            [m = std::move(message),
             self = shared_from_this(),
             moreHandler = std::forward<MoreHandler>(moreHandler)](auto ec) mutable {
                if (ec) { self->connection.cancel(); }
                if (moreHandler) moreHandler(ec);
            });
    }

    connection_type connection;
    varlink_service& service_;
    std::error_code send_ec{};
};

} // namespace varlink
#endif // LIBVARLINK_SERVER_SESSION_HPP
