#pragma once
#include <varlink/client.hpp>

using namespace varlink;

#ifdef VARLINK_TEST_ASYNC
#include <varlink/server.hpp>
using test_server = varlink_server;
#else
#include <varlink/threaded_server.hpp>
using test_server = threaded_server;
#endif

class BaseEnvironment {
  protected:
    net::io_context ctx{};
    std::unique_ptr<test_server> server;
#ifdef VARLINK_TEST_ASYNC
    std::unique_ptr<net::steady_timer> timer;
    std::thread worker;
#endif

  public:
    virtual ~BaseEnvironment()
    {
#ifdef VARLINK_TEST_ASYNC
        if (worker.joinable()) {
            ctx.stop();
            worker.join();
        }
#endif
    }

#ifdef VARLINK_TEST_ASYNC
    net::steady_timer& get_timer() { return *timer; }
#endif

    void add_interface(std::string_view iface, callback_map&& cb)
    {
        if (server) server->add_interface(iface, std::move(cb));
    }
};
