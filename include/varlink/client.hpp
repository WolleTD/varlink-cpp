/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <charconv>
#include <string>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
using callmode = varlink_message::callmode;

template <
    typename Socket,
    typename = std::enable_if_t<std::is_base_of_v<net::socket_base, Socket>>>
class basic_varlink_client {
  public:
    using socket_type = Socket;
    using protocol_type = typename socket_type::protocol_type;
    using executor_type = typename socket_type::executor_type;
    using Connection = json_connection<Socket>;

    socket_type& socket() { return connection.socket(); }
    [[nodiscard]] const socket_type& socket() const
    {
        return connection.socket();
    }

    executor_type get_executor() { return socket().get_executor(); }

  private:
    Connection connection;
    detail::manual_strand<executor_type> call_strand;

  public:
    explicit basic_varlink_client(Socket socket)
        : connection(std::move(socket)), call_strand(get_executor())
    {
    }
    explicit basic_varlink_client(Connection&& existing_connection)
        : connection(std::move(existing_connection)), call_strand(get_executor())
    {
    }

    template <ASIO_COMPLETION_TOKEN_FOR(
        void(std::error_code, std::shared_ptr<connection_type>))
                  ReplyHandler ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)>
    auto async_call(
        const varlink_message& message,
        ReplyHandler&& handler ASIO_DEFAULT_COMPLETION_TOKEN(executor_type))
    {
        call_strand.push([this,
                          &message,
                          handler = std::forward<ReplyHandler>(handler)]() mutable {
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
                        async_read_reply(
                            more, std::forward<ReplyHandler>(handler));
                    }
                });
        });
    }

    template <typename ReplyHandler>
    void async_read_reply(bool wants_more, ReplyHandler&& handler)
    {
        connection.async_receive(
            [this, wants_more, handler = std::forward<ReplyHandler>(handler)](
                auto ec, json reply) mutable {
                const auto continues =
                    (wants_more and reply.contains("continues")
                     and reply["continues"].get<bool>());
                if (reply.contains("error")) {
                    ec = net::error::no_data;
                }
                if (not ec and continues) {
                    async_read_reply(wants_more, handler);
                }
                if (ec == net::error::try_again) {
                    ec = std::error_code{};
                }
                else if (not continues) {
                    call_strand.next();
                }
                handler(ec, reply["parameters"], continues);
            });
    }

    std::function<json()> call(const varlink_message& message)
    {
        connection.send(message.json_data());

        return [this,
                continues = not message.oneway(),
                more = message.more()]() mutable -> json {
            if (continues) {
                json reply = connection.receive();
                if (reply.contains("error")) {
                    continues = false;
                    throw varlink_error(
                        reply["error"].get<std::string>(), reply["parameters"]);
                }
                continues =
                    (more and reply.contains("continues")
                     and reply["continues"].get<bool>());
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

using varlink_client_unix =
    basic_varlink_client<net::local::stream_protocol::socket>;
using varlink_client_tcp = basic_varlink_client<net::ip::tcp::socket>;
using varlink_client_variant =
    std::variant<varlink_client_unix, varlink_client_tcp>;

class varlink_client {
  private:
    net::io_context ctx{};
    varlink_client_variant client;

    auto make_client(const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> varlink_client_variant {
                using Socket =
                    typename std::decay_t<decltype(sockaddr)>::protocol_type::socket;
                auto socket = Socket{ctx, sockaddr.protocol()};
                socket.connect(sockaddr);
                return basic_varlink_client(std::move(socket));
            },
            endpoint_from_uri(uri));
    }

  public:
    explicit varlink_client(const varlink_uri& uri) : client(make_client(uri))
    {
    }
    explicit varlink_client(std::string_view uri)
        : varlink_client(varlink_uri(uri))
    {
    }
    explicit varlink_client(varlink_client_variant&& conn)
        : client(std::move(conn))
    {
    }
    template <
        typename Socket,
        typename = std::enable_if_t<std::is_base_of_v<net::socket_base, Socket>>>
    explicit varlink_client(Socket&& socket)
        : client(basic_varlink_client(std::forward<Socket>(socket)))
    {
    }

    varlink_client(const varlink_client& src) = delete;
    varlink_client& operator=(const varlink_client&) = delete;
    varlink_client(varlink_client&& src) noexcept = delete;
    varlink_client& operator=(varlink_client&& src) noexcept = delete;

    template <typename... Args>
    auto async_call(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call(std::forward<Args>(args)...); },
            client);
    }

    std::function<json()> call(const varlink_message& message)
    {
        return std::visit([&](auto&& c) { return c.call(message); }, client);
    }
    std::function<json()> call(
        const std::string& method,
        const json& parameters,
        callmode mode = callmode::basic)
    {
        return call(varlink_message(method, parameters, mode));
    }
};
} // namespace varlink

#endif
