#include <filesystem>
#include <catch2/catch.hpp>
#include <varlink/client.hpp>

#ifdef VARLINK_TEST_TCP
#include "self_tcp.hpp"
#elif VARLINK_TEST_UNIX
#include "self_unix.hpp"
#else
#error "No transport type specified"
#endif

using namespace varlink;
using std::string;

std::unique_ptr<BaseEnvironment> getEnvironment() {
    return std::make_unique<Environment>();
}

#define REQUIRE_VARLINK_ERROR(statement, error, parameter, value) \
    try {                                                         \
        statement;                                                \
        REQUIRE_THROWS_AS(statement, varlink_error);              \
    }                                                             \
    catch (varlink_error & e) {                                   \
        REQUIRE(std::string(e.what()) == error);                  \
        REQUIRE(e.args()[parameter].get<string>() == value);      \
    }

TEST_CASE("Testing server with client")
{
    auto client = varlink_client(Environment::varlink_uri);

    SECTION("Call method GetInfo")
    {
        auto resp = client.call("org.varlink.service.GetInfo", {})();
        REQUIRE(resp["vendor"].get<string>() == "varlink");
        REQUIRE(resp["product"].get<string>() == "test");
        REQUIRE(resp["version"].get<string>() == "1");
        REQUIRE(resp["url"].get<string>() == "test.org");
        // resp = client.call("org.varlink.service.GetInfo", {{"value", "should
        // fail"}})(); REQUIRE(resp["error"].get<string>() ==
        // "org.varlink.service.InvalidArgument"); REQUIRE(resp["parameter"].get<string>()
        // == "value");
    }

    SECTION("Call method GetInterfaceDescription")
    {
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

    SECTION("Call method org.test.Ping")
    {
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

    SECTION("Call method org.test.More without more flag")
    {
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.M", {{"n", 5}}, callmode::basic)(),
            "org.varlink.service.MethodNotImplemented",
            "method",
            "org.test.M");
    }

    SECTION("Call method org.test.More wtih more flag")
    {
        auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
        REQUIRE(more()["m"].get<int>() == 0);
        REQUIRE(more()["m"].get<int>() == 1);
        REQUIRE(more()["m"].get<int>() == 2);
        REQUIRE(more()["m"].get<int>() == 3);
        REQUIRE(more()["m"].get<int>() == 4);
        REQUIRE(more()["m"].get<int>() == 5);
        REQUIRE(more() == nullptr);
    }

    SECTION("Don't multiplex responses")
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        auto more = client.call(
            "org.test.M", {{"n", 1}, {"t", true}}, callmode::more);
        REQUIRE(more()["m"].get<int>() == 0);
        auto resp = client.call("org.test.P", {{"p", "test"}});
        REQUIRE(more()["m"].get<int>() == 1);
        REQUIRE(resp()["q"].get<string>() == "test");
        REQUIRE(more() == nullptr);
    }

    SECTION("Call method and don't read")
    {
        client.call("org.test.P", {{"p", "test"}});
    }

    SECTION("Call method org.test.More and don't read all responses")
    {
        auto more = client.call("org.test.M", {{"n", 5}}, callmode::more);
        REQUIRE(more()["m"].get<int>() == 0);
        REQUIRE(more()["m"].get<int>() == 1);
        REQUIRE(more()["m"].get<int>() == 2);
    }

    SECTION("Call a oneway method")
    {
        auto null = client.call("org.test.P", {{"p", "test"}}, callmode::oneway);
        REQUIRE(null() == nullptr);
    }

    SECTION("Call with upgrade flag")
    {
        auto resp = client.call(
            "org.test.P", {{"p", "test"}}, callmode::upgrade)();
        REQUIRE(resp["q"].get<string>() == "test");
    }

    SECTION("Call a method on a non-existent interface")
    {
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

    SECTION("Call a non-existing method")
    {
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.NonExistent", {})(),
            "org.varlink.service.MethodNotFound",
            "method",
            "org.test.NonExistent");
    }

    SECTION("Call a method that generates an internal exception")
    {
        try {
            client.call("org.err.E", {})();
        }
        catch (varlink_error& e) {
            REQUIRE(
                std::string(e.what()) == "org.varlink.service.InternalError");
        }
    }

    SECTION("Concurrent handling of two connections")
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        auto client2 = varlink_client(Environment::varlink_uri);
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

    SECTION("Noop connections")
    {
        auto client1 = varlink_client(Environment::varlink_uri);
        auto client2 = varlink_client(Environment::varlink_uri);
        auto client3 = varlink_client(Environment::varlink_uri);
        auto client4 = varlink_client(Environment::varlink_uri);
    }
}

TEST_CASE("Testing server with raw socket data")
{
    using proto = Environment::protocol;
    net::io_context ctx;
    auto endpoint = Environment::get_endpoint();
    auto socket = proto::socket(ctx, endpoint.protocol());
    socket.connect(endpoint);

    SECTION("Send an invalid varlink message")
    {
        std::string data = R"({"notmethod":"org.test.NonExistent"})";
        auto buffer = net::buffer(data.data(), data.size() + 1);
        socket.send(buffer);
        REQUIRE_THROWS_AS((void)socket.receive(buffer), std::system_error);
    }

    SECTION("Parameters is not an object")
    {
        std::string data = R"({"method":"org.test.P","parameters":["array"]})";
        auto buffer = net::buffer(data.data(), data.size() + 1);
        socket.send(buffer);
        REQUIRE_THROWS_AS((void)socket.receive(buffer), std::system_error);
    }

    SECTION("Invalid json")
    {
        std::string data = "{totally_not\"a json-message]";
        auto buffer = net::buffer(data.data(), data.size() + 1);
        socket.send(buffer);
        REQUIRE_THROWS_AS((void)socket.receive(buffer), std::system_error);
    }

    SECTION("Incomplete message")
    {
        std::string data = R"({"method":"org.)";
        auto buffer = net::buffer(data.data(), data.size());
        socket.send(buffer);
        REQUIRE_THROWS_AS((void)socket.receive(buffer), std::system_error);
    }

    SECTION("Don't lose multiple messages in buffer")
    {
        std::string data = R"({"method":"org.not.found"})";
        data += '\0';
        data += R"({"method":"org.not.found"})";
        socket.send(net::buffer(data.data(), data.size() + 1));
        data.resize(174);
        auto n = socket.receive(net::buffer(data.data(), data.size()));
        socket.receive(net::buffer(data.data() + n, data.size() - n));
        std::string exp =
            R"({"error":"org.varlink.service.InterfaceNotFound","parameters":{"interface":"org.not"}})";
        exp += '\0';
        exp += R"({"error":"org.varlink.service.InterfaceNotFound","parameters":{"interface":"org.not"}})";
        exp += '\0';
        REQUIRE(exp == data);
    }
}
