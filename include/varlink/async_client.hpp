#ifndef LIBVARLINK_ASYNC_CLIENT_HPP
#define LIBVARLINK_ASYNC_CLIENT_HPP

#include <variant>
#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>
#include <varlink/json_connection.hpp>

namespace varlink {
template <typename Protocol>
class async_client {
  public:
    using protocol_type = Protocol;
    using socket_type = typename protocol_type::socket;
    using endpoint_type = typename protocol_type::endpoint;
    using executor_type = typename socket_type::executor_type;
    using connection_type = json_connection<protocol_type>;

    socket_type& socket() { return connection.socket(); }
    [[nodiscard]] const socket_type& socket() const { return connection.socket(); }

    executor_type get_executor() { return connection.get_executor(); }

    explicit async_client(const asio::any_io_executor& ex) : async_client(socket_type(ex)) {}
    explicit async_client(asio::io_context& ctx) : async_client(socket_type(ctx)) {}

    explicit async_client(socket_type socket)
        : connection(std::move(socket)), call_strand(get_executor())
    {
    }

    explicit async_client(connection_type&& existing_connection)
        : connection(std::move(existing_connection)), call_strand(get_executor())
    {
    }

    template <typename ConnectHandler>
    decltype(auto) async_connect(const endpoint_type& endpoint, ConnectHandler&& handler)
    {
        return connection.async_connect(endpoint, std::forward<ConnectHandler>(handler));
    }

    void connect(const endpoint_type& endpoint) { connection.connect(endpoint); }
    void connect(const endpoint_type& endpoint, std::error_code& ec)
    {
        connection.connect(endpoint, ec);
    }

    void close() { connection.close(); }
    void close(std::error_code& ec) { connection.close(ec); }

    void cancel() { connection.cancel(); }
    void cancel(std::error_code& ec) { connection.cancel(ec); }

    bool is_open() { return connection.is_open(); }

    template <typename ReplyHandler>
    auto async_call(const varlink_message& message, ReplyHandler&& handler)
    {
        return net::async_initiate<ReplyHandler, void(std::error_code)>(
            initiate_async_call<callmode::basic>(this), handler, message);
    }

    template <typename ReplyHandler>
    auto async_call(std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        auto message = varlink_message(method, parameters);
        return async_call(message, std::forward<ReplyHandler>(handler));
    }

    template <typename ReplyHandler>
    auto async_call_more(const varlink_message_more& message, ReplyHandler&& handler)
    {
        return net::async_initiate<ReplyHandler, void(std::error_code)>(
            initiate_async_call<callmode::more>(this), handler, message);
    }

    template <typename ReplyHandler>
    auto async_call_more(std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        auto message = varlink_message_more(method, parameters);
        return async_call_more(message, std::forward<ReplyHandler>(handler));
    }

    template <typename ReplyHandler>
    auto async_call_oneway(const varlink_message_oneway& message, ReplyHandler&& handler)
    {
        return net::async_initiate<ReplyHandler, void(std::error_code)>(
            initiate_async_call<callmode::oneway>(this), handler, message);
    }

    template <typename ReplyHandler>
    auto async_call_oneway(std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        auto message = varlink_message_oneway(method, parameters);
        return async_call_oneway(message, std::forward<ReplyHandler>(handler));
    }

    template <typename ReplyHandler>
    auto async_call_upgrade(const varlink_message_upgrade& message, ReplyHandler&& handler)
    {
        return net::async_initiate<ReplyHandler, void(std::error_code)>(
            initiate_async_call<callmode::upgrade>(this), handler, message);
    }

    template <typename ReplyHandler>
    auto async_call_upgrade(std::string_view method, const json& parameters, ReplyHandler&& handler)
    {
        auto message = varlink_message_upgrade(method, parameters);
        return async_call_upgrade(message, std::forward<ReplyHandler>(handler));
    }

    json call(const varlink_message& message) { return call_impl(message)(); }

    json call(std::string_view method, const json& parameters)
    {
        return call(varlink_message(method, parameters));
    }

    std::function<json()> call_more(const varlink_message_more& message)
    {
        return call_impl(message);
    }

    std::function<json()> call_more(std::string_view method, const json& parameters)
    {
        return call_more(varlink_message_more(method, parameters));
    }

    void call_oneway(const varlink_message_oneway& message) { (void)call_impl(message)(); }

    void call_oneway(std::string_view method, const json& parameters)
    {
        call_oneway(varlink_message_oneway(method, parameters));
    }

    json call_upgrade(const varlink_message_upgrade& message) { return call_impl(message)(); }

    json call_upgrade(std::string_view method, const json& parameters)
    {
        return call_upgrade(varlink_message_upgrade(method, parameters));
    }

  private:
    connection_type connection;
    detail::manual_strand<executor_type> call_strand;

    std::function<json()> call_impl(const basic_varlink_message& message)
    {
        connection.send(message.json_data());

        return [this, mode = message.mode()]() mutable -> json {
            if (mode == callmode::oneway) { return nullptr; }
            else {
                json reply = connection.receive();
                if (reply.contains("error")) {
                    mode = callmode::oneway;
                    throw varlink_error(reply["error"].get<std::string>(), reply["parameters"]);
                }
                if (mode != callmode::more or not reply_continues(reply)) {
                    mode = callmode::oneway;
                }
                return reply["parameters"];
            }
        };
    }

    template <callmode CallMode, typename ReplyHandler>
    void async_read_reply(ReplyHandler&& handler)
    {
        static_assert(CallMode != callmode::oneway);
        connection.async_receive(
            [this, handler = std::forward<ReplyHandler>(handler)](auto ec, json reply) mutable {
                if (reply.contains("error")) {
                    ec = make_varlink_error(reply["error"].get<std::string>());
                }
                if constexpr (CallMode == callmode::more) {
                    const auto continues = (not ec and reply_continues(reply));
                    handler(ec, reply["parameters"], continues);
                    if (continues) {
                        async_read_reply<CallMode>(std::forward<ReplyHandler>(handler));
                    }
                    else {
                        call_strand.next();
                    }
                }
                else {
                    handler(ec, reply["parameters"]);
                    call_strand.next();
                }
            });
    }

    template <callmode CallMode>
    class initiate_async_call {
      private:
        async_client* self_;

      public:
        explicit initiate_async_call(async_client* self) : self_(self) {}

        template <typename CompletionHandler>
        void operator()(CompletionHandler&& handler, const typed_varlink_message<CallMode>& message)
        {
            self_->call_strand.push(
                [self = self_, message, handler = std::forward<CompletionHandler>(handler)]() mutable {
                    self->connection.async_send(
                        message.json_data(),
                        [self, handler = std::forward<CompletionHandler>(handler)](auto ec) mutable {
                            if constexpr (CallMode == callmode::oneway) {
                                self->call_strand.next();
                                return handler(ec);
                            }
                            else if (ec) {
                                self->call_strand.next();
                                if constexpr (CallMode == callmode::more) {
                                    return handler(ec, json{}, false);
                                }
                                else {
                                    return handler(ec, json{});
                                }
                            }
                            else {
                                self->template async_read_reply<CallMode>(
                                    std::forward<CompletionHandler>(handler));
                            }
                        });
                });
        }
    };
};

using varlink_client_unix = async_client<net::local::stream_protocol>;
using varlink_client_tcp = async_client<net::ip::tcp>;
using varlink_client_variant = std::variant<varlink_client_unix, varlink_client_tcp>;

} // namespace varlink
#endif // LIBVARLINK_ASYNC_CLIENT_HPP
