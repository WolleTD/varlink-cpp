#define CATCH_CONFIG_RUNNER
#include <filesystem>
#include <catch2/catch.hpp>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
using std::string;

class Environment {
    const std::string_view varlink_uri{"tcp:127.0.0.1:51337"};
    const varlink_service::description description{
        "varlink",
        "test",
        "1",
        "test.org"};
    std::unique_ptr<threaded_server> server;

  public:
    Environment()
    {
        const auto testif =
            "interface org.test\nmethod P(p:string) -> (q:string)\n"
            "method M(n:int,t:?bool)->(m:int)\n";
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

#define REQUIRE_VARLINK_ERROR(statement, error, parameter, value) \
    try {                                                         \
        statement;                                                \
        REQUIRE_THROWS_AS(statement, varlink_error);              \
    }                                                             \
    catch (varlink_error & e) {                                   \
        REQUIRE(std::string(e.what()) == error);                  \
        REQUIRE(e.args()[parameter].get<string>() == value);      \
    }

TEST_CASE("TCPSocket, GetInfo")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto resp = client.call("org.varlink.service.GetInfo", {})();
    REQUIRE(resp["vendor"].get<string>() == "varlink");
    REQUIRE(resp["product"].get<string>() == "test");
    REQUIRE(resp["version"].get<string>() == "1");
    REQUIRE(resp["url"].get<string>() == "test.org");
    // resp = client.call("org.varlink.service.GetInfo", {{"value", "should fail"}})();
    // REQUIRE(resp["error"].get<string>() == "org.varlink.service.InvalidArgument");
    // REQUIRE(resp["parameter"].get<string>() == "value");
}

TEST_CASE("TCPSocket, GetInterfaceDescription")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto resp = client.call(
        "org.varlink.service.GetInterfaceDescription",
        {{"interface", "org.test"}})();
    REQUIRE(resp["description"].get<string>() ==
            "interface org.test\n\n"
            "method P(p: string) -> (q: string)\n\n"
            "method M(n: int, t: ?bool) -> (m: int)\n");
    REQUIRE_VARLINK_ERROR(
        client.call(
            "org.varlink.service.GetInterfaceDescription",
            {{"interface", "org.notfound"}})(),
        "org.varlink.service.InterfaceNotFound",
        "interface",
        "org.notfound");
    REQUIRE_VARLINK_ERROR(
        client.call("org.varlink.service.GetInterfaceDescription", {})(),
        "org.varlink.service.InvalidParameter",
        "parameter",
        "interface");
}

TEST_CASE("TCPSocket, orgtestPing")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto resp = client.call("org.test.P", {{"p", "test"}})();
    REQUIRE(resp["q"].get<string>() == "test");
    REQUIRE_VARLINK_ERROR(
        client.call("org.test.P", {{"q", "invalid"}})(),
        "org.varlink.service.InvalidParameter",
        "parameter",
        "p");
    REQUIRE_VARLINK_ERROR(
        client.call("org.test.P", {{"p", 20}})(),
        "org.varlink.service.InvalidParameter",
        "parameter",
        "p");
}

TEST_CASE("TCPSocket, orgtestMore")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(more()["m"].get<int>() == 2);
    REQUIRE(more()["m"].get<int>() == 3);
    REQUIRE(more()["m"].get<int>() == 4);
    REQUIRE(more()["m"].get<int>() == 5);
    REQUIRE(more() == nullptr);
    REQUIRE_VARLINK_ERROR(
        client.call("org.test.M", {{"n", 5}}, callmode::basic)(),
        "org.varlink.service.MethodNotImplemented",
        "method",
        "org.test.M");
}

TEST_CASE("TCPSocket, orgtestMoreThread")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto client2 = varlink_client("tcp:127.0.0.1:51337");
    auto begin = steady_clock::now();
    auto more = client.call(
        "org.test.M", {{"n", 1}, {"t", true}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    auto begin2 = steady_clock::now();
    auto resp = client2.call("org.test.P", {{"p", "test"}})();
    auto done2 = steady_clock::now();
    REQUIRE(resp["q"].get<string>() == "test");
    REQUIRE(more()["m"].get<int>() == 1);
    auto done = steady_clock::now();
    REQUIRE(more() == nullptr);
    auto latency = done - begin;
    auto latency2 = done2 - begin2;
    REQUIRE(latency2 < latency);
}

TEST_CASE("TCPSocket, orgtestMoreNoMultiplex")
{
    using namespace std::chrono;
    using namespace std::chrono_literals;
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto more = client.call(
        "org.test.M", {{"n", 1}, {"t", true}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    auto resp = client.call("org.test.P", {{"p", "test"}});
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(resp()["q"].get<string>() == "test");
    REQUIRE(more() == nullptr);
}

TEST_CASE("TCPSocket, orgtestMoreAndQuit")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
    REQUIRE(more()["m"].get<int>() == 0);
    REQUIRE(more()["m"].get<int>() == 1);
    REQUIRE(more()["m"].get<int>() == 2);
}

TEST_CASE("TCPSocket, orgtestDontread")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    client.call("org.test.P", {{"p", "test"}});
}

TEST_CASE("TCPSocket, orgtestOneway")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto null = client.call("org.test.P", {{"p", "test"}}, callmode::oneway);
    REQUIRE(null() == nullptr);
}

TEST_CASE("TCPSocket, Upgrade")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    auto resp = client.call("org.test.P", {{"p", "test"}}, callmode::upgrade)();
    REQUIRE(resp["q"].get<string>() == "test");
}

TEST_CASE("TCPSocket, InterfaceNotFound")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    REQUIRE_VARLINK_ERROR(
        client.call("org.notfound.NonExistent", {})(),
        "org.varlink.service.InterfaceNotFound",
        "interface",
        "org.notfound");
    REQUIRE_VARLINK_ERROR(
        client.call("org.notfound", {})(),
        "org.varlink.service.InterfaceNotFound",
        "interface",
        "org");
    REQUIRE_VARLINK_ERROR(
        client.call("org", {})(),
        "org.varlink.service.InterfaceNotFound",
        "interface",
        "org");
}

TEST_CASE("TCPSocket, MethodNotFound")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    REQUIRE_VARLINK_ERROR(
        client.call("org.test.NonExistent", {})(),
        "org.varlink.service.MethodNotFound",
        "method",
        "org.test.NonExistent");
}

TEST_CASE("TCPSocket, InvalidMessage")
{
    using proto = net::ip::tcp;
    net::io_context ctx;
    auto endpoint = proto::endpoint(
        net::ip::make_address_v4("127.0.0.1"), 51337);
    auto socket = proto::socket(ctx, endpoint.protocol());
    socket.connect(endpoint);
    auto client = json_connection_tcp(std::move(socket));
    client.send(R"({"notmethod":"org.test.NonExistent"})"_json);
    REQUIRE_THROWS_AS((void)client.receive(), std::system_error);
}

TEST_CASE("TCPSocket, InvalidParameterType")
{
    using proto = net::ip::tcp;
    net::io_context ctx;
    auto endpoint = proto::endpoint(
        net::ip::make_address_v4("127.0.0.1"), 51337);
    auto socket = proto::socket(ctx, endpoint.protocol());
    socket.connect(endpoint);
    auto client = json_connection_tcp(std::move(socket));
    client.send(R"({"method":"org.test.P","parameters":["array"]})"_json);
    REQUIRE_THROWS_AS((void)client.receive(), std::system_error);
}

TEST_CASE("TCPSocket, NotJsonMessage")
{
    using net::ip::tcp;
    net::io_context ctx{};
    auto client = tcp::socket(ctx);
    auto endpoint = tcp::endpoint(net::ip::make_address_v4("127.0.0.1"), 51337);
    client.open(endpoint.protocol());
    client.connect(endpoint);
    std::string data = "{totally_not\"a json-message]";
    client.send(net::buffer(data.data(), data.size() + 1));
    REQUIRE_THROWS_AS(
        (void)client.receive(net::buffer(data.data(), data.size())),
        std::system_error);
}

TEST_CASE("TCPSocket, Incomplete Message")
{
    using net::ip::tcp;
    net::io_context ctx{};
    auto client = tcp::socket(ctx);
    auto endpoint = tcp::endpoint(net::ip::make_address_v4("127.0.0.1"), 51337);
    client.open(endpoint.protocol());
    client.connect(endpoint);
    std::string data = R"({"method":"org.)";
    client.send(net::buffer(data.data(), data.size()));
    REQUIRE_THROWS_AS(
        (void)client.receive(net::buffer(data.data(), data.size())),
        std::system_error);
}

TEST_CASE("TCPSocket, MultipleMessagesInBuffer")
{
    using net::ip::tcp;
    net::io_context ctx{};
    auto client = tcp::socket(ctx);
    auto endpoint = tcp::endpoint(net::ip::make_address_v4("127.0.0.1"), 51337);
    client.open(endpoint.protocol());
    client.connect(endpoint);
    std::string data = R"({"method":"org.not.found"})";
    data += '\0';
    data += R"({"method":"org.not.found"})";
    client.send(net::buffer(data.data(), data.size() + 1));
    data.resize(174);
    auto n = client.receive(net::buffer(data.data(), data.size()));
    client.receive(net::buffer(data.data() + n, data.size() - n));
    std::string exp =
        R"({"error":"org.varlink.service.InterfaceNotFound","parameters":{"interface":"org.not"}})";
    exp += '\0';
    exp += R"({"error":"org.varlink.service.InterfaceNotFound","parameters":{"interface":"org.not"}})";
    exp += '\0';
    REQUIRE(exp == data);
}

TEST_CASE("TCPSocket, Nothing")
{
    auto client1 = varlink_client("tcp:127.0.0.1:51337");
    auto client2 = varlink_client("tcp:127.0.0.1:51337");
    auto client3 = varlink_client("tcp:127.0.0.1:51337");
    auto client4 = varlink_client("tcp:127.0.0.1:51337");
}

TEST_CASE("TCPSocket, InternalException")
{
    auto client = varlink_client("tcp:127.0.0.1:51337");
    try {
        client.call("org.err.E", {})();
    }
    catch (varlink_error& e) {
        REQUIRE(std::string(e.what()) == "org.varlink.service.InternalError");
    }
}

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = Environment();
    return Catch::Session().run(argc, argv);
}
