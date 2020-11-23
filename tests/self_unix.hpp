#pragma once
#include "test_env.hpp"

class UnixEnvironment : public BaseEnvironment {
  public:
    using protocol = net::local::stream_protocol;
    static constexpr const std::string_view varlink_uri{
#ifdef VARLINK_TEST_ASYNC
        "unix:test-integration-async.socket"
#else
        "unix:test-integration.socket"
#endif
    };
    static protocol::endpoint get_endpoint()
    {
        return protocol::endpoint(
#ifdef VARLINK_TEST_ASYNC
            "test-integration-async.socket"
#else
            "test-integration.socket"
#endif
        );
    }

  private:
    const varlink_service::description description{
        "varlink",
        "test",
        "1",
        "test.org"};

  public:
    UnixEnvironment() : BaseEnvironment()
    {
        std::filesystem::remove(get_endpoint().path());
#ifdef VARLINK_TEST_ASYNC
        server = std::make_unique<test_server>(ctx, varlink_uri, description);
        timer = std::make_unique<net::steady_timer>(server->get_executor());
        server->async_serve_forever();
        worker = std::thread([&]() { ctx.run(); });
#else
        server = std::make_unique<test_server>(varlink_uri, description);
#endif
    }
};

using Environment = UnixEnvironment;
