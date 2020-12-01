#ifndef LIBVARLINK_ASYNC_CLIENT_HPP
#define LIBVARLINK_ASYNC_CLIENT_HPP

#include <variant>
#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>
#include <varlink/json_connection.hpp>

namespace varlink {
using callmode = varlink_message::callmode;

template <typename Socket>
class async_client {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using Connection = json_connection<Socket>;

    socket_type& socket() { return connection.socket(); }
    [[nodiscard]] const socket_type& socket() const { return connection.socket(); }

    executor_type get_executor() { return socket().get_executor(); }

  private:
    Connection connection;
    detail::manual_strand<executor_type> call_strand;

  public:
    explicit async_client(Socket socket)
        : connection(std::move(socket)), call_strand(get_executor())
    {
    }
    explicit async_client(Connection&& existing_connection)
        : connection(std::move(existing_connection)), call_strand(get_executor())
    {
    }

    template <VARLINK_COMPLETION_TOKEN_FOR(void(std::error_code, std::shared_ptr<connection_type>))
                  ReplyHandler VARLINK_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_call(
        const varlink_message& message,
        ReplyHandler&& handler VARLINK_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        call_strand.push([this, message, handler = std::forward<ReplyHandler>(handler)]() mutable {
            connection.async_send(
                message.json_data(),
                [this,
                 oneway = message.oneway(),
                 more = message.more(),
                 handler = std::forward<ReplyHandler>(handler)](auto ec) mutable {
                    if (ec) {
                        call_strand.next();
                        return handler(ec, json{}, false);
                    }
                    if (oneway) {
                        call_strand.next();
                        return handler(net::error_code{}, json{}, false);
                    }
                    else {
                        async_read_reply(more, std::forward<ReplyHandler>(handler));
                    }
                });
        });
    }

    template <typename ReplyHandler>
    void async_read_reply(bool wants_more, ReplyHandler&& handler)
    {
        connection.async_receive([this, wants_more, handler = std::forward<ReplyHandler>(handler)](
                                     auto ec, json reply) mutable {
            const auto continues =
                (wants_more and reply.contains("continues") and reply["continues"].get<bool>());
            if (reply.contains("error")) { ec = net::error::no_data; }
            if (not continues) { call_strand.next(); }
            else if (not ec and not connection.data_available()) {
                async_read_reply(wants_more, handler);
            }
            handler(ec, reply["parameters"], continues);
        });
    }

    std::function<json()> call(const varlink_message& message)
    {
        connection.send(message.json_data());

        return [this, continues = not message.oneway(), more = message.more()]() mutable -> json {
            if (continues) {
                json reply = connection.receive();
                if (reply.contains("error")) {
                    continues = false;
                    throw varlink_error(reply["error"].get<std::string>(), reply["parameters"]);
                }
                continues = (more and reply.contains("continues") and reply["continues"].get<bool>());
                return reply["parameters"];
            }
            else {
                return nullptr;
            }
        };
    }

    std::function<json()> call(
        const std::string& method,
        const json& parameters,
        callmode mode = callmode::basic)
    {
        return call(varlink_message(method, parameters, mode));
    }
};

using varlink_client_unix = async_client<net::local::stream_protocol::socket>;
using varlink_client_tcp = async_client<net::ip::tcp::socket>;
using varlink_client_variant = std::variant<varlink_client_unix, varlink_client_tcp>;

} // namespace varlink
#endif // LIBVARLINK_ASYNC_CLIENT_HPP
