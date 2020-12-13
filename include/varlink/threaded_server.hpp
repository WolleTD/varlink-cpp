#ifndef LIBVARLINK_THREADED_SERVER_HPP
#define LIBVARLINK_THREADED_SERVER_HPP

#include <varlink/async_server.hpp>
#include <varlink/service.hpp>
#include <varlink/uri.hpp>

namespace varlink {
class threaded_server {
  private:
    net::thread_pool ctx;
    varlink_service service;
    async_server_variant server;

    auto make_async_server(const varlink_uri& uri)
    {
        return std::visit(
            [&](auto&& sockaddr) -> async_server_variant {
                using Acceptor = typename std::decay_t<decltype(sockaddr)>::protocol_type::acceptor;
                return async_server(Acceptor{ctx, sockaddr}, service);
            },
            endpoint_from_uri(uri));
    }

  public:
    threaded_server(const varlink_uri& uri, const varlink_service::description& description)
        : ctx(4), service(description), server(make_async_server(uri))
    {
        std::visit([&](auto&& s) { net::post(ctx, [&]() { s.async_serve_forever(); }); }, server);
    }

    threaded_server(std::string_view uri, const varlink_service::description& description)
        : threaded_server(varlink_uri(uri), description)
    {
    }

    threaded_server(const threaded_server& src) = delete;
    threaded_server& operator=(const threaded_server&) = delete;
    threaded_server(threaded_server&& src) noexcept = delete;
    threaded_server& operator=(threaded_server&& src) noexcept = delete;

    template <typename... Args>
    void add_interface(Args&&... args)
    {
        service.add_interface(std::forward<Args>(args)...);
    }

    auto get_executor() { return ctx.get_executor(); }

    void stop() { ctx.stop(); }

    void join() { ctx.join(); }
};

} // namespace varlink
#endif // LIBVARLINK_THREADED_SERVER_HPP
