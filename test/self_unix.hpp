#pragma once
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
using std::string;

class BaseEnvironment {
  public:
    virtual ~BaseEnvironment() = default;
};

class UnixEnvironment : public BaseEnvironment {
  public:
    using protocol = net::local::stream_protocol;
    static constexpr const std::string_view varlink_uri{"unix:test-integration.socket"};
    static protocol::endpoint get_endpoint() {
        return protocol::endpoint("test-integration.socket");
    }
  private:
    const varlink_service::description description{
        "varlink",
        "test",
        "1",
        "test.org"};
    std::unique_ptr<threaded_server> server;

  public:
    UnixEnvironment() : BaseEnvironment()
    {
        const auto testif =
            "interface org.test\nmethod P(p:string) -> (q:string)\n"
            "method M(n:int,t:?bool)->(m:int)\n";
        std::filesystem::remove("test-integration.socket");
        server = std::make_unique<threaded_server>(varlink_uri, description);
        auto ping_callback = [] varlink_callback {
            return {{"q", parameters["p"].get<string>()}};
        };
        auto more_callback = [] varlink_callback {
            const auto count = parameters["n"].get<int>();
            const bool wait = parameters.contains("t")
                              && parameters["t"].get<bool>();
            for (auto i = 0; i < count; i++) {
                sendmore({{"m", i}});
                if (wait)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {{"m", count}};
        };
        server->add_interface(
            testif, callback_map{{"P", ping_callback}, {"M", more_callback}});
        server->add_interface(
            "interface org.err\nmethod E() -> ()\n",
            callback_map{{"E", [] varlink_callback { throw std::exception{}; }}});
    }
};

using Environment = UnixEnvironment;
