#include <iostream>
#include <varlink/server.hpp>

#include "org.example.more.varlink.hpp"

using namespace varlink;

struct example_more_server {
    explicit example_more_server(const std::string& uri)
        : server(
              ctx,
              uri,
              varlink_service::description{"Varlink", "More example", "1", "https://varlink.org"}),
          timer(ctx)
    {
        varlink_service::interface handler(org_example_more_varlink);
        handler.add_callback("Ping", ping);
        handler.add_callback("TestMore", &example_more_server::more, this);
        handler.add_callback("StopServing", &example_more_server::stop, this);

        server.add_interface(std::move(handler));
    }

    static json ping(const json& parameters, callmode) { return {{"pong", parameters["ping"]}}; }

    void more(const json& parameters, callmode mode, reply_function&& send_reply)
    {
        if (mode == callmode::more) {
            json state = {{"start", true}};
            auto n = parameters["n"].get<size_t>();
            send_reply({{"state", state}}, [this, send_reply, n](auto) {
                json state;
                state["progress"] = 0;
                send_reply({{"state", state}}, [this, send_reply, n](auto) {
                    start_timer(1, n, send_reply);
                });
            });
        }
        else {
            throw varlink_error("org.varlink.service.InvalidParameter", {{"parameter", "more"}});
        }
    };

    json stop(const json&, callmode)
    {
        ctx.stop();
        return json::object();
    }

    void run()
    {
        server.async_serve_forever();
        ctx.run();
    }

  private:
    void start_timer(int i, int count, const reply_function& send_reply)
    {
        timer.expires_after(std::chrono::seconds(1));
        timer.async_wait([this, i, count, send_reply](auto ec) mutable {
            if (not ec) {
                json state = json::object();
                state["progress"] = (100 / count) * i;
                send_reply({{"state", state}}, [this](auto ec) {
                    if (ec) { timer.cancel(); }
                });
                if (i < count) { start_timer(i + 1, count, send_reply); }
                else {
                    state.erase("progress");
                    state["end"] = true;
                    send_reply({{"state", state}}, nullptr);
                }
            }
        });
    }

    net::io_context ctx;
    varlink_server server;
    net::steady_timer timer;
};

std::unique_ptr<example_more_server> service;

void signalHandler(int32_t)
{
    service.reset(nullptr);
    exit(0);
}

int main()
{
    using namespace varlink;
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGPIPE, SIG_IGN);
    try {
        service = std::make_unique<example_more_server>("unix:/tmp/test.socket");
        service->run();
        return 0;
    }
    catch (varlink_error& e) {
        std::cerr << "Couldn't start service: " << e.what() << e.args() << "\n";
        return 1;
    }
    catch (std::exception& e) {
        std::cerr << "Couldn't start service: " << e.what() << "\n";
        return 1;
    }
}
