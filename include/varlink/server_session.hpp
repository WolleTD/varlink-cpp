#ifndef LIBVARLINK_SERVER_SESSION_HPP
#define LIBVARLINK_SERVER_SESSION_HPP

#include <varlink/json_connection.hpp>
#include <varlink/service.hpp>

namespace varlink {
template <typename Protocol>
class server_session : public std::enable_shared_from_this<server_session<Protocol>> {
  public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<protocol_type>;

    using std::enable_shared_from_this<server_session<Protocol>>::shared_from_this;

    executor_type get_executor() { return connection.get_executor(); }

  private:
    connection_type connection;
    varlink_service& service_;
    std::error_code send_ec{};

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
            if (ec) return;
            try {
                const basic_varlink_message message{j};
                self->service_.message_call(message, [self](const json& reply) {
                    if (self->send_ec) { throw std::system_error(self->send_ec); }
                    if (reply.is_object()) {
                        auto m = std::make_unique<json>(reply);
                        self->async_send_reply(std::move(m));
                    }
                    if (not reply_continues(reply)) { self->start(); }
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
            if (ec) {
                self->connection.cancel();
                self->send_ec = ec;
            }
        });
    }
};

} // namespace varlink
#endif // LIBVARLINK_SERVER_SESSION_HPP
