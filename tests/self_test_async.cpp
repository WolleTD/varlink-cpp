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

void start_timer(net::steady_timer& timer, int i, int count, const reply_function& send_reply)
{
    timer.expires_after(std::chrono::milliseconds(10));
    timer.async_wait([&timer, i, continues = (i < count), count, send_reply](auto ec) mutable {
        if (not ec) {
            send_reply({{"m", i}}, continues);
            if (continues) start_timer(timer, i + 1, count, send_reply);
        }
    });
}

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
    auto more_callback = [&timer = env->get_timer()] varlink_callback {
        const auto count = parameters["n"].get<int>();
        const bool wait = parameters.contains("t") && parameters["t"].get<bool>();
        send_reply({{"m", 0}}, true);
        if (wait) { start_timer(timer, 1, count, send_reply); }
        else {
            for (auto i = 1; i <= count; i++) {
                send_reply({{"m", i}}, (i < count));
            }
        }
    };
    auto empty_callback = [] varlink_callback { send_reply({}, false); };
    env->add_interface(
        testif, callback_map{{"P", ping_callback}, {"M", more_callback}, {"E", empty_callback}});
    env->add_interface(
        "interface org.err\nmethod E() -> ()\n",
        callback_map{{"E", [] varlink_callback { throw std::exception{}; }}});
    return env;
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

using test_client = async_client<Environment::protocol>;
using socket_type = typename test_client::socket_type;

TEST_CASE("Testing server with client")
{
    asio::io_context ctx{};
    auto client = varlink_client(ctx, Environment::varlink_uri);

    SECTION("Call method GetInfo")
    {
        bool flag{false};
        auto msg = varlink_message("org.varlink.service.GetInfo", {});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["vendor"].get<string>() == "varlink");
            REQUIRE(resp["product"].get<string>() == "test");
            REQUIRE(resp["version"].get<string>() == "1");
            REQUIRE(resp["url"].get<string>() == "test.org");
            flag = true;
        });
        // resp = client.call("org.varlink.service.GetInfo", {{"value", "should
        // fail"}})(); REQUIRE(resp["error"].get<string>() ==
        // "org.varlink.service.InvalidArgument"); REQUIRE(resp["parameter"].get<string>()
        // == "value");
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method GetInterfaceDescription")
    {
        bool flag{false};
        auto msg = varlink_message(
            "org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["description"].get<string>() ==
                "interface org.test\n\n"
                "method P(p: string) -> (q: string)\n\n"
                "method M(n: int, t: ?bool) -> (m: int)\n\n"
                "method E() -> ()\n");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method GetInterfaceDescription with unknown interface")
    {
        bool flag{false};
        auto msg = varlink_message(
            "org.varlink.service.GetInterfaceDescription", {{"interface", "org.notfound"}});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.InterfaceNotFound");
            REQUIRE(resp["interface"].get<std::string>() == "org.notfound");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method GetInterfaceDescription without paramter")
    {
        bool flag{false};
        auto msg = varlink_message("org.varlink.service.GetInterfaceDescription", {});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.InvalidParameter");
            REQUIRE(resp["parameter"].get<std::string>() == "interface");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method org.test.Ping")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.P", {{"p", "test"}});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<string>() == "test");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method org.test.Ping with invalid parameter")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.P", {{"q", "invalid"}});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.InvalidParameter");
            REQUIRE(resp["parameter"].get<std::string>() == "p");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method org.test.More without more flag")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.M", {{"n", 5}});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.MethodNotImplemented");
            REQUIRE(resp["method"].get<std::string>() == "org.test.M");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call method org.test.More wtih more flag")
    {
        int flag{0};
        auto msg = varlink_message_more("org.test.M", {{"n", 5}});
        client.async_call_more(msg, [&](auto ec, const json& resp, bool c) {
            REQUIRE(not ec);
            REQUIRE(c == (flag < 5));
            REQUIRE(flag++ == resp["m"].get<int>());
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag == 6);
    }

    SECTION("Don't multiplex responses")
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        int flag{0};
        steady_clock::time_point more_done{};
        steady_clock::time_point ping_done{};
        auto msg1 = varlink_message_more("org.test.M", {{"n", 5}, {"t", true}});
        client.async_call_more(msg1, [&](auto ec, const json& resp, bool c) {
            REQUIRE(not ec);
            REQUIRE(c == (flag < 5));
            REQUIRE(flag++ == resp["m"].get<int>());
            if (not c) { more_done = steady_clock::now(); }
        });
        auto msg2 = varlink_message("org.test.P", {{"p", "test"}});
        client.async_call(msg2, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<string>() == "test");
            ping_done = steady_clock::now();
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag == 6);
        REQUIRE(more_done < ping_done);
    }

    SECTION("Call method with empty response")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.E", json::object());
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp.is_object());
            REQUIRE(resp.empty());
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call a oneway method")
    {
        bool flag{false};
        auto msg = varlink_message_oneway("org.test.P", {{"p", "test"}});
        client.async_call_oneway(msg, [&](auto ec) {
            REQUIRE(not ec);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call a oneway method and then a regular one")
    {
        bool flag1{false};
        bool flag2{false};
        auto msg1 = varlink_message_oneway("org.test.P", {{"p", "test"}});
        client.async_call_oneway(msg1, [&](auto ec) {
            REQUIRE(not ec);
            flag1 = true;
        });
        auto msg2 = varlink_message("org.test.P", {{"p", "test"}});
        client.async_call(msg2, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<std::string>() == "test");
            flag2 = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag1);
        REQUIRE(flag2);
    }

    SECTION("Call with upgrade flag")
    {
        bool flag{false};
        auto msg = varlink_message_upgrade("org.test.P", {{"p", "test"}});
        client.async_call_upgrade(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<string>() == "test");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call a method on a non-existent interface")
    {
        bool flag{false};
        auto msg = varlink_message("org.notfound.NonExistent", {});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.InterfaceNotFound");
            REQUIRE(resp["interface"].get<std::string>() == "org.notfound");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Call a non-existing method")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.NonExistent", {});
        client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(ec.category() == varlink_category());
            REQUIRE(ec.message() == "org.varlink.service.MethodNotFound");
            REQUIRE(resp["method"].get<std::string>() == "org.test.NonExistent");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    /*
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
     */

    SECTION("Concurrent handling of two connections")
    {
        using namespace std::chrono;
        using namespace std::chrono_literals;
        auto socket2 = socket_type(ctx, Environment::get_endpoint().protocol());
        socket2.connect(Environment::get_endpoint());
        auto client2 = test_client(std::move(socket2));
        auto begin = steady_clock::now();
        steady_clock::duration more_duration{};
        steady_clock::duration ping_duration{};
        bool flag{false};
        int more_cnt = 0;
        auto more = varlink_message_more("org.test.M", {{"n", 1}, {"t", true}});
        client.async_call_more(more, [&](auto ec, const json& resp, bool c) {
            REQUIRE(not ec);
            REQUIRE(resp["m"].get<int>() == more_cnt++);
            if (not c) more_duration = steady_clock::now() - begin;
        });
        auto ping = varlink_message("org.test.P", {{"p", "test"}});
        client2.async_call(ping, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<string>() == "test");
            ping_duration = steady_clock::now() - begin;
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(more_cnt == 2);
        REQUIRE(flag);
        REQUIRE(ping_duration < more_duration * 0.8);
    }

    SECTION("Move client and call method org.test.Ping")
    {
        bool flag{false};
        auto msg = varlink_message("org.test.P", {{"p", "test"}});
        auto my_client = std::move(client);
        my_client.async_call(msg, [&](auto ec, const json& resp) {
            REQUIRE(not ec);
            REQUIRE(resp["q"].get<string>() == "test");
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
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
}
