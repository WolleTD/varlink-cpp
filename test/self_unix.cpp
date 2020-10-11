#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <filesystem>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
using std::string;

class Environment {
    const std::string_view varlink_uri{"unix:test-integration.socket"};
    const varlink_service::description description{"varlink", "test", "1", "test.org"};
    std::unique_ptr<threaded_server> server;

   public:
    Environment() {
        const auto testif =
            "interface org.test\nmethod P(p:string) -> (q:string)\n"
            "method M(n:int,t:?bool)->(m:int)\n";
        std::filesystem::remove("test-integration.socket");
        server = std::make_unique<threaded_server>(varlink_uri, description);
        auto ping_callback = [] varlink_callback { return {{"q", parameters["p"].get<string>()}}; };
        auto more_callback = [] varlink_callback {
            const auto count = parameters["n"].get<int>();
            const bool wait = parameters.contains("t") && parameters["t"].get<bool>();
            for (auto i = 0; i < count; i++) {
                sendmore({{"m", i}});
                if (wait) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return {{"m", count}};
        };
        server->add_interface(testif, callback_map{{"P", ping_callback}, {"M", more_callback}});
        server->add_interface("interface org.err\nmethod E() -> ()\n",
                              callback_map{{"E", [] varlink_callback { throw std::exception{}; }}});
    }
};

#define REQUIRE_VARLINK_ERROR(statement, error, parameter, value) \
    try {                                                         \
        statement;                                                \
        REQUIRE_THROWS_AS(statement, varlink_error);              \
    } catch (varlink_error & e) {                                 \
        REQUIRE(std::string(e.what()) == error);                  \
        REQUIRE(e.args()[parameter].get<string>() == value);      \
    }

TEST_CASE("UnixSocket, GetInfo") {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInfo", {})();
    REQUIRE(resp["vendor"].get<string>() == "varlink");
    REQUIRE(resp["product"].get<string>() == "test");
    REQUIRE(resp["version"].get<string>() == "1");
    REQUIRE(resp["url"].get<string>() == "test.org");
    // resp = client.call("org.varlink.service.GetInfo", {{"value", "should fail"}})();
    // REQUIRE(resp["error"].get<string>() == "org.varlink.service.InvalidArgument");
    // REQUIRE(resp["parameter"].get<string>() == "value");
}

TEST_CASE("UnixSocket, GetInterfaceDescription") {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})();
    REQUIRE(resp["description"].get<string>() ==
            "interface org.test\n\n"
            "method P(p: string) -> (q: string)\n\n"
            "method M(n: int, t: ?bool) -> (m: int)\n");
    REQUIRE_VARLINK_ERROR(client.call("org.varlink.service.GetInterfaceDescription", {{"interface", "org.notfound"}})(),
                          "org.varlink.service.InterfaceNotFound", "interface", "org.notfound");
    REQUIRE_VARLINK_ERROR(client.call("org.varlink.service.GetInterfaceDescription", {})(),
                          "org.varlink.service.InvalidParameter", "parameter", "interface");
}

TEST_CASE("UnixSocket, orgtestPing") {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.test.P", {{"p", "test"}})();
    REQUIRE(resp["q"].get<string>() == "test");
    REQUIRE_VARLINK_ERROR(client.call("org.test.P", {{"q", "invalid"}})(), "org.varlink.service.InvalidParameter",
                          "parameter", "p");
    REQUIRE_VARLINK_ERROR(client.call("org.test.P", {{"p", 20}})(), "org.varlink.service.InvalidParameter", "parameter",
                          "p");
}

TEST_CASE("UnixSocket, orgtestMore") {
    auto client = varlink_client("unix:test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(more()["m"].get<int>() == 2);
    REQUIRE(more()["m"].get<int>() == 3);
    REQUIRE(more()["m"].get<int>() == 4);
    REQUIRE(more()["m"].get<int>() == 5);
    REQUIRE(more() == nullptr);
    REQUIRE_VARLINK_ERROR(client.call("org.test.M", {{"n", 5}}, callmode::basic)(),
                          "org.varlink.service.MethodNotImplemented", "method", "org.test.M");
}

TEST_CASE("UnixSocket, orgtestMoreThread") {
    using namespace std::chrono;
    using namespace std::chrono_literals;
    auto client = varlink_client("unix:test-integration.socket");
    auto client2 = varlink_client("unix:test-integration.socket");
    auto begin = steady_clock::now();
    auto more = client.call("org.test.M", {{"n", 1}, {"t", true}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    auto begin2 = steady_clock::now();
    auto resp = client2.call("org.test.P", {{"p", "test"}})();
    auto done2 = steady_clock::now();
    REQUIRE(resp["q"].get<string>() == "test");
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(more() == nullptr);
    auto time_to_send = begin2 - begin;
    auto latency = done2 - begin2;
    REQUIRE(time_to_send < 1ms);
    REQUIRE(latency < 1ms);
}
TEST_CASE("UnixSocket, orgtestMoreAndQuit") {
    auto client = varlink_client("unix:test-integration.socket");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(more()["m"].get<int>() == 2);
}

TEST_CASE("UnixSocket, orgtestDontread") {
    auto client = varlink_client("unix:test-integration.socket");
    client.call("org.test.P", {{"p", "test"}});
}

TEST_CASE("UnixSocket, orgtestOneway") {
    auto client = varlink_client("unix:test-integration.socket");
    auto null = client.call("org.test.P", {{"p", "test"}}, callmode::oneway);
    REQUIRE(null() == nullptr);
}

TEST_CASE("UnixSocket, Upgrade") {
    auto client = varlink_client("unix:test-integration.socket");
    auto resp = client.call("org.test.P", {{"p", "test"}}, callmode::upgrade)();
    REQUIRE(resp["q"].get<string>() == "test");
}

TEST_CASE("UnixSocket, InterfaceNotFound") {
    auto client = varlink_client("unix:test-integration.socket");
    REQUIRE_VARLINK_ERROR(client.call("org.notfound.NonExistent", {})(), "org.varlink.service.InterfaceNotFound",
                          "interface", "org.notfound");
    REQUIRE_VARLINK_ERROR(client.call("org.notfound", {})(), "org.varlink.service.InterfaceNotFound", "interface",
                          "org");
    REQUIRE_VARLINK_ERROR(client.call("org", {})(), "org.varlink.service.InterfaceNotFound", "interface", "org");
}

TEST_CASE("UnixSocket, MethodNotFound") {
    auto client = varlink_client("unix:test-integration.socket");
    REQUIRE_VARLINK_ERROR(client.call("org.test.NonExistent", {})(), "org.varlink.service.MethodNotFound", "method",
                          "org.test.NonExistent");
}

TEST_CASE("UnixSocket, InvalidMessage") {
    using proto = asio::local::stream_protocol;
    asio::io_context ctx;
    auto endpoint = proto::endpoint("test-integration.socket");
    auto socket = proto::socket(ctx, endpoint.protocol());
    socket.connect(endpoint);
    auto client = json_connection_unix(std::move(socket));
    client.send(R"({"notmethod":"org.test.NonExistent"})"_json);
    REQUIRE_THROWS_AS((void)client.receive(), std::system_error);
}

TEST_CASE("UnixSocket, InvalidParameterType") {
    using proto = asio::local::stream_protocol;
    asio::io_context ctx;
    auto endpoint = proto::endpoint("test-integration.socket");
    auto socket = proto::socket(ctx, endpoint.protocol());
    socket.connect(endpoint);
    auto client = json_connection_unix(std::move(socket));
    client.send(R"({"method":"org.test.P","parameters":["array"]})"_json);
    REQUIRE_THROWS_AS((void)client.receive(), std::system_error);
}

TEST_CASE("UnixSocket, NotJsonMessage") {
    using asio::local::stream_protocol;
    asio::io_context ctx{};
    auto client = stream_protocol::socket(ctx);
    auto endpoint = stream_protocol::endpoint("test-integration.socket");
    client.open(endpoint.protocol());
    client.connect(endpoint);
    std::string data = "{totally_not\"a json-message]";
    client.send(asio::buffer(data.data(), data.size() + 1));
    REQUIRE_THROWS_AS((void)client.receive(asio::buffer(data.data(), data.size())), std::system_error);
}

TEST_CASE("UnixSocket, Nothing") {
    auto client1 = varlink_client("unix:test-integration.socket");
    auto client2 = varlink_client("unix:test-integration.socket");
    auto client3 = varlink_client("unix:test-integration.socket");
    auto client4 = varlink_client("unix:test-integration.socket");
}

TEST_CASE("UnixSocket, InternalException") {
    auto client = varlink_client("unix:test-integration.socket");
    try {
        client.call("org.err.E", {})();
    } catch (varlink_error& e) {
        REQUIRE(std::string(e.what()) == "org.varlink.service.InternalError");
    }
}

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    auto env = Environment();
    return Catch::Session().run(argc, argv);
}
