/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <varlink/async_server.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

namespace varlink {
class varlink_server {
    auto make_async_server(net::io_context& ctx, const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> async_server_variant {
                return server_t<decltype(sockaddr)>({ctx, sockaddr}, service);
            },
            endpoint_from_uri(uri));
    }

  public:
    varlink_server(net::io_context& ctx, const varlink_uri& uri, const varlink_service::description& description)
        : service(description), server(make_async_server(ctx, uri))
    {
    }

    varlink_server(net::io_context& ctx, std::string_view uri, const varlink_service::description& description)
        : varlink_server(ctx, varlink_uri(uri), description)
    {
    }

    varlink_server(const varlink_server& src) = delete;
    varlink_server& operator=(const varlink_server&) = delete;
    varlink_server(varlink_server&& src) noexcept = delete;
    varlink_server& operator=(varlink_server&& src) noexcept = delete;

    void async_serve_forever()
    {
        std::visit([&](auto&& s) { s.async_serve_forever(); }, server);
    }

    template <typename... Args>
    void add_interface(Args&&... args)
    {
        service.add_interface(std::forward<Args>(args)...);
    }

    auto get_executor()
    {
        return std::visit([](auto&& s) { return s.get_executor(); }, server);
    }

  private:
    varlink_service service;
    async_server_variant server;
};
} // namespace varlink

#endif
