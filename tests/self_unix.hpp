#pragma once
#include "test_env.hpp"
#include <experimental/filesystem>

struct UnixEnvironment : BaseEnvironment {
    using protocol = net::local::stream_protocol;
    static constexpr std::string_view varlink_uri{
#ifdef VARLINK_TEST_ASYNC
        "unix:test-integration-async.socket"
#else
        "unix:test-integration.socket"
#endif
    };
    static protocol::endpoint get_endpoint()
    {
        return {
#ifdef VARLINK_TEST_ASYNC
            "test-integration-async.socket"
#else
            "test-integration.socket"
#endif
        };
    }

    UnixEnvironment() : BaseEnvironment()
    {
        std::experimental::filesystem::remove(get_endpoint().path());
#ifdef VARLINK_TEST_ASYNC
        server = std::make_unique<test_server>(ctx, varlink_uri, description);
        timer = std::make_unique<net::steady_timer>(server->get_executor());
        server->async_serve_forever();
        worker = std::thread([&]() { ctx.run(); });
#else
        server = std::make_unique<test_server>(varlink_uri, description);
#endif
    }

  private:
    const varlink_service::description description{"varlink", "test", "1", "test.org"};
};

using Environment = UnixEnvironment;
