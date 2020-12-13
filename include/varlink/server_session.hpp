#ifndef LIBVARLINK_SERVER_SESSION_HPP
#define LIBVARLINK_SERVER_SESSION_HPP

#include <varlink/json_connection.hpp>
#include <varlink/service.hpp>

namespace varlink {
template <typename Socket>
class server_session : public std::enable_shared_from_this<server_session<Socket>> {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<socket_type>;

    using std::enable_shared_from_this<server_session<Socket>>::shared_from_this;

    socket_type& socket() { return connection.socket(); }
    [[nodiscard]] const socket_type& socket() const { return connection.socket(); }

    executor_type get_executor() { return socket().get_executor(); }

  private:
    json_connection<socket_type> connection;
    varlink_service& service_;

  public:
    explicit server_session(socket_type socket, varlink_service& service)
        : connection(std::move(socket)), service_(service)
    {
    }

    server_session(const server_session&) = delete;
    server_session& operator=(const server_session&) = delete;
    server_session(server_session&&) noexcept = default;
    server_session& operator=(server_session&&) noexcept = default;

    void start()
    {
        connection.async_receive([self = shared_from_this()](auto ec, auto&& j) {
            if (ec and ec != net::error::try_again) return;
            try {
                const varlink_message message{j};
                self->service_.message_call(message, [self, ec](const json& reply, bool continues) {
                    if (reply.is_object()) {
                        auto m = std::make_unique<json>(reply);
                        self->async_send_reply(std::move(m));
                    }
                    if (not continues and (ec != net::error::try_again)) { self->start(); }
                });
            }
            catch (...) {
            }
        });
    }

  private:
    void async_send_reply(std::unique_ptr<json> message)
    {
        auto& data = *message;
        connection.async_send(data, [m = std::move(message), self = shared_from_this()](auto ec) {
            if (ec) { self->connection.socket().cancel(); }
        });
    }
};

} // namespace varlink
#endif // LIBVARLINK_SERVER_SESSION_HPP
