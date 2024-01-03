#ifndef LIBVARLINK_SERVER_SESSION_HPP
#define LIBVARLINK_SERVER_SESSION_HPP

#include <varlink/json_connection.hpp>
#include <varlink/service.hpp>

namespace varlink {
using exception_handler = std::function<void(std::exception_ptr)>;

template <typename Protocol>
struct server_session : std::enable_shared_from_this<server_session<Protocol>> {
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<protocol_type>;

    using std::enable_shared_from_this<server_session>::shared_from_this;

    explicit server_session(socket_type socket, varlink_service& service, exception_handler ex_handler)
        : connection(std::move(socket)), service_(service), ex_handler_(std::move(ex_handler))
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
            try {
                if (ec) throw std::system_error(ec);

                const basic_varlink_message message{j};
                self->service_.message_call(
                    message,
                    [self](const std::exception_ptr& eptr, const json& reply, more_handler&& handler) {
                        const auto continues = static_cast<bool>(handler);
                        if (reply.is_object()) {
                            auto m = std::make_unique<json>(reply);
                            self->async_send_reply(eptr, std::move(m), std::move(handler));
                        }
                        if (not continues) { self->start(); }
                    });
            }
            catch (...) {
                if (self->ex_handler_) self->ex_handler_(std::current_exception());
            }
        });
    }

  private:
    void async_send_reply(
        const std::exception_ptr& eptr,
        std::unique_ptr<json> message,
        more_handler&& handler)
    {
        auto& data = *message;
        connection.async_send(
            data,
            [eptr = eptr,
             m = std::move(message),
             self = shared_from_this(),
             handler = std::move(handler)](auto ec) mutable {
                if (ec) eptr = std::make_exception_ptr(std::system_error(ec));

                if (eptr and self->ex_handler_) { self->ex_handler_(eptr); }
                else if (handler) {
                    handler(eptr);
                }
            });
    }

    connection_type connection;
    varlink_service& service_;
    exception_handler ex_handler_;
};

} // namespace varlink
#endif // LIBVARLINK_SERVER_SESSION_HPP
