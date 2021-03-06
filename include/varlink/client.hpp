/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_CLIENT_HPP
#define LIBVARLINK_VARLINK_CLIENT_HPP

#include <varlink/async_client.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

namespace varlink {
class varlink_client {
  private:
    varlink_client_variant client;

    static auto make_client(net::io_context& ctx, const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> varlink_client_variant {
                using Socket = typename std::decay_t<decltype(sockaddr)>::protocol_type::socket;
                auto socket = Socket{ctx, sockaddr.protocol()};
                socket.connect(sockaddr);
                return async_client(std::move(socket));
            },
            endpoint_from_uri(uri));
    }

  public:
    explicit varlink_client(net::io_context& ctx, const varlink_uri& uri)
        : client(make_client(ctx, uri))
    {
    }
    explicit varlink_client(net::io_context& ctx, std::string_view uri)
        : varlink_client(ctx, varlink_uri(uri))
    {
    }
    template <typename Socket, typename = std::enable_if_t<std::is_base_of_v<net::socket_base, Socket>>>
    explicit varlink_client(Socket&& socket) : client(async_client(std::forward<Socket>(socket)))
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
            [&](auto&& c) { return c.async_call(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    auto async_call_more(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_more(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    auto async_call_oneway(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_oneway(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    auto async_call_upgrade(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.async_call_upgrade(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    json call(Args&&... args)
    {
        return std::visit([&](auto&& c) { return c.call(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    std::function<json()> call_more(Args&&... args)
    {
        return std::visit([&](auto&& c) { return c.call_more(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    void call_oneway(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.call_oneway(std::forward<Args>(args)...); }, client);
    }

    template <typename... Args>
    json call_upgrade(Args&&... args)
    {
        return std::visit(
            [&](auto&& c) { return c.call_upgrade(std::forward<Args>(args)...); }, client);
    }
};
} // namespace varlink

#endif
