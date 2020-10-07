#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <varlink/server.hpp>

#include "org.example.more.varlink.hpp"

using namespace varlink;

class example_more_server {
   private:
    varlink_server _server;

   public:
    explicit example_more_server(const std::string& uri)
        : _server(uri, varlink_service::description{"Varlink", "More example", "1", "https://varlink.org"}) {

        auto ping = [] varlink_callback { return {{"pong", parameters["ping"]}}; };

        auto more = [] varlink_callback {
            if (sendmore) {
                nlohmann::json state = {{"start", true}};
                sendmore({{"state", state}});
                state.erase("start");
                auto n = parameters["n"].get<size_t>();
                for (size_t i = 0; i < n; i++) {
                    state["progress"] = (100 / n) * i;
                    sendmore({{"state", state}});
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                state["progress"] = 100;
                sendmore({{"state", state}});
                state.erase("progress");
                state["end"] = true;
                return {{"state", state}};
            } else {
                throw varlink::varlink_error("org.varlink.service.InvalidParameter", {{"parameter", "more"}});
            }
        };

        _server.add_interface(varlink::org_example_more_varlink, callback_map{{"Ping", ping}, {"TestMore", more}});
    }

    void join() { _server.join(); }
};

std::unique_ptr<example_more_server> service;

void signalHandler(int32_t) {
    service.reset(nullptr);
    exit(0);
}

int main() {
    using namespace varlink;
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGPIPE, SIG_IGN);
    try {
        service = std::make_unique<example_more_server>("unix:/tmp/test.socket");
        service->join();
        return 0;
    } catch (std::exception& e) {
        std::cerr << "Couldn't start service: " << e.what() << "\n";
        return 1;
    }
}
