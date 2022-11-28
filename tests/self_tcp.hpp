#pragma once
#include "test_env.hpp"

class TCPEnvironment : public BaseEnvironment {
  public:
    using protocol = net::ip::tcp;
    static constexpr const std::string_view varlink_uri{
#ifdef VARLINK_TEST_ASYNC
        "tcp:127.0.0.1:61337"
#else
        "tcp:127.0.0.1:51337"
#endif
    };
    static protocol::endpoint get_endpoint()
    {
        return {
            net::ip::make_address_v4("127.0.0.1"),
#ifdef VARLINK_TEST_ASYNC
            61337
#else
            51337
#endif
        };
    }

  private:
    const varlink_service::description description{"varlink", "test", "1", "test.org"};

  public:
    TCPEnvironment() : BaseEnvironment()
    {
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

using Environment = TCPEnvironment;
