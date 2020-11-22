#pragma once
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;

#ifdef VARLINK_TEST_ASYNC
using test_server = managed_async_server;
#else
using test_server = threaded_server;
#endif

class BaseEnvironment {
  protected:
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
            server->stop();
            worker.join();
        }
#endif
    }

#ifdef VARLINK_TEST_ASYNC
    net::steady_timer& get_timer() { return *timer; }
#endif

    void add_interface(std::string_view iface, const callback_map& cb)
    {
        if (server)
            server->add_interface(iface, cb);
    }
};
