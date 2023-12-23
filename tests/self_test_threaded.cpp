#include <thread>
#include <catch2/catch_test_macros.hpp>
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

std::unique_ptr<BaseEnvironment> getEnvironment()
{
    auto env = std::make_unique<Environment>();
    const auto testif =
        "interface org.test\nmethod P(p:string) -> (q:string)\n"
        "method M(n:int,t:?bool)->(m:int)\n"
        "method E()->()\n";
    auto ping_callback = [] varlink_callback {
        send_reply({{"q", parameters["p"].get<string>()}}, false);
    };
    auto more_callback = [] varlink_callback {
        const auto count = parameters["n"].get<int>();
        const bool wait = parameters.contains("t") && parameters["t"].get<bool>();
        for (auto i = 0; i < count; i++) {
            send_reply({{"m", i}}, true);
            if (wait) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        send_reply({{"m", count}}, false);
    };
    auto empty_callback = [] varlink_callback { send_reply({}, false); };
    env->add_interface(
        testif, callback_map{{"P", ping_callback}, {"M", more_callback}, {"E", empty_callback}});
    env->add_interface(
        "interface org.err\nmethod E() -> ()\n",
        callback_map{{"E", [] varlink_callback { throw std::exception{}; }}});
    return env;
}

#define REQUIRE_VARLINK_ERROR(statement, error, parameter, value)  \
    do {                                                           \
        try {                                                      \
            statement;                                             \
            REQUIRE_THROWS_AS(statement, varlink_error);           \
        }                                                          \
        catch (varlink_error & e) {                                \
            REQUIRE(std::string(e.what()) == (error));             \
            REQUIRE(e.args()[parameter].get<string>() == (value)); \
        }                                                          \
    } while (false)

TEST_CASE("Testing server with client")
{
    net::io_context ctx{};
    auto client = varlink_client(ctx, Environment::varlink_uri);

    SECTION("Call method GetInfo")
    {
        auto resp = client.call("org.varlink.service.GetInfo", json{});
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
            "org.varlink.service.GetInterfaceDescription", json{{"interface", "org.test"}});
        REQUIRE(resp["description"].get<string>() ==
                "interface org.test\n\n"
                "method P(p: string) -> (q: string)\n\n"
                "method M(n: int, t: ?bool) -> (m: int)\n\n"
                "method E() -> ()\n");
        REQUIRE_VARLINK_ERROR(
            client.call(
                "org.varlink.service.GetInterfaceDescription", json{{"interface", "org.notfound"}}),
            "org.varlink.service.InterfaceNotFound",
            "interface",
            "org.notfound");
        REQUIRE_VARLINK_ERROR(
            client.call("org.varlink.service.GetInterfaceDescription", json{}),
            "org.varlink.service.InvalidParameter",
            "parameter",
            "interface");
    }

    SECTION("Call method org.test.Ping")
    {
        auto resp = client.call("org.test.P", json{{"p", "test"}});
        REQUIRE(resp["q"].get<string>() == "test");
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.P", json{{"q", "invalid"}}),
            "org.varlink.service.InvalidParameter",
            "parameter",
            "p");
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.P", json{{"p", 20}}),
            "org.varlink.service.InvalidParameter",
            "parameter",
            "p");
    }

    SECTION("Call method org.test.More without more flag")
    {
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.M", json{{"n", 5}}),
            "org.varlink.service.MethodNotImplemented",
            "method",
            "org.test.M");
    }

    SECTION("Call method org.test.More wtih more flag")
    {
        auto more = client.call_more("org.test.M", json{{"n", 5}});
        REQUIRE(more()["m"].get<int>() == 0);
        REQUIRE(more()["m"].get<int>() == 1);
        REQUIRE(more()["m"].get<int>() == 2);
        REQUIRE(more()["m"].get<int>() == 3);
        REQUIRE(more()["m"].get<int>() == 4);
        REQUIRE(more()["m"].get<int>() == 5);
        REQUIRE(more() == nullptr);
    }

    SECTION("Call method with empty response")
    {
        auto resp = client.call("org.test.E", json::object());
        REQUIRE(resp.is_object());
        REQUIRE(resp.empty());
    }

    SECTION("Call method and don't read") { client.call("org.test.P", json{{"p", "test"}}); }

    SECTION("Call method org.test.More and don't read all responses")
    {
        auto more = client.call_more("org.test.M", json{{"n", 5}});
        REQUIRE(more()["m"].get<int>() == 0);
        REQUIRE(more()["m"].get<int>() == 1);
        REQUIRE(more()["m"].get<int>() == 2);
    }

    SECTION("Call a oneway method and then a regular one")
    {
        client.call_oneway("org.test.P", json{{"p", "test"}});
        auto resp = client.call("org.test.E", json::object());
        REQUIRE(resp.is_object());
        REQUIRE(resp.empty());
    }

    SECTION("Call with upgrade flag")
    {
        auto resp = client.call_upgrade("org.test.P", json{{"p", "test"}});
        REQUIRE(resp["q"].get<string>() == "test");
    }

    SECTION("Call a method on a non-existent interface")
    {
        REQUIRE_VARLINK_ERROR(
            client.call("org.notfound.NonExistent", json{}),
            "org.varlink.service.InterfaceNotFound",
            "interface",
            "org.notfound");
        REQUIRE_VARLINK_ERROR(
            client.call("org.notfound", json{}),
            "org.varlink.service.InterfaceNotFound",
            "interface",
            "org");
        REQUIRE_VARLINK_ERROR(
            client.call("org", json{}), "org.varlink.service.InterfaceNotFound", "interface", "org");
    }

    SECTION("Call a non-existing method")
    {
        REQUIRE_VARLINK_ERROR(
            client.call("org.test.NonExistent", json{}),
            "org.varlink.service.MethodNotFound",
            "method",
            "org.test.NonExistent");
    }

    SECTION("Call a method that generates an internal exception")
    {
        try {
            client.call("org.err.E", json{});
        }
        catch (varlink_error& e) {
            REQUIRE(std::string(e.what()) == "org.varlink.service.InternalError");
        }
    }

    SECTION("Concurrent handling of two connections")
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        auto client2 = varlink_client(ctx, Environment::varlink_uri);
        auto begin = steady_clock::now();
        auto more = client.call_more("org.test.M", json{{"n", 1}, {"t", true}});
        REQUIRE(more()["m"].get<int>() == 0);
        auto begin2 = steady_clock::now();
        auto resp = client2.call("org.test.P", json{{"p", "test"}});
        auto done2 = steady_clock::now();
        REQUIRE(resp["q"].get<string>() == "test");
        REQUIRE(more()["m"].get<int>() == 1);
        auto done = steady_clock::now();
        REQUIRE(more() == nullptr);
        auto latency = done - begin;
        auto latency2 = done2 - begin2;
        REQUIRE(latency2 < latency * 0.8);
    }

    SECTION("Noop connections")
    {
        auto client1 = varlink_client(ctx, Environment::varlink_uri);
        auto client2 = varlink_client(ctx, Environment::varlink_uri);
        auto client3 = varlink_client(ctx, Environment::varlink_uri);
        auto client4 = varlink_client(ctx, Environment::varlink_uri);
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

    SECTION("Partial transmission")
    {
        std::string data = R"({"method":"org.)";
        socket.send(net::buffer(data.data(), data.size()));
        data = R"(varlink.service.GetInf"})";
        socket.send(net::buffer(data.data(), data.size() + 1));
        data.resize(100);
        socket.receive(net::buffer(data.data(), data.size()));
        std::string exp =
            R"({"error":"org.varlink.service.MethodNotFound","parameters":{"method":"org.varlink.service.GetInf"}})";
        exp += '\0';
        REQUIRE(exp == data);
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

    SECTION("Don't multiplex responses")
    {
        std::string data = R"({"method":"org.test.M","parameters":{"n":1,"t":true},"more":true})";
        data += '\0';
        data += R"({"method":"org.test.P","parameters":{"p":"test"}})";
        socket.send(net::buffer(data.data(), data.size() + 1));
        std::string exp = R"({"continues":true,"parameters":{"m":0}})";
        exp += '\0';
        exp += R"({"continues":false,"parameters":{"m":1}})";
        exp += '\0';
        exp += R"({"parameters":{"q":"test"}})";
        data.resize(exp.size());
        net::read(socket, net::buffer(data.data(), data.size()));
        REQUIRE(exp == data);
    }
}
