#include <future>
#include <thread>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <varlink/json_connection.hpp>

#include "fake_socket.hpp"

#ifdef LIBVARLINK_USE_BOOST
#include <boost/asio/thread_pool.hpp>
#else
#include <asio/thread_pool.hpp>
#endif

using namespace varlink;
using test_connection = json_connection<fake_proto>;
using Catch::Approx;

TEST_CASE("JSON transport sync read")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    auto setup_test = [&](auto&&... args) -> test_connection {
        (socket.setup_fake(args), ...);
        return test_connection(std::move(socket));
    };

    SECTION("Read basic JSON")
    {
        auto conn = setup_test(
            R"({"object":true})",
            R"("string")",
            R"({"int":42})",
            R"({"float":3.14})",
            R"(["array"])",
            R"(null)");
        REQUIRE(conn.receive()["object"].get<bool>() == true);
        REQUIRE(conn.receive().get<std::string>() == "string");
        REQUIRE(conn.receive()["int"].get<int>() == 42);
        REQUIRE(conn.receive()["float"].get<double>() == Approx(3.14));
        REQUIRE(conn.receive()[0].get<std::string>() == "array");
        REQUIRE(conn.receive().is_null() == true);
    }

    SECTION("Read partial")
    {
        socket.write_max = 10;
        auto conn = setup_test(R"({"object":true})");
        REQUIRE(conn.receive()["object"].get<bool>() == true);
    }

    SECTION("Throw on partial json")
    {
        auto conn = setup_test(R"({"object":)");
        REQUIRE_THROWS_AS((void)conn.receive(), std::invalid_argument);
    }

    SECTION("Throw on trailing bytes")
    {
        auto conn = setup_test(R"({"object":true}trailing)");
        REQUIRE_THROWS_AS((void)conn.receive(), std::invalid_argument);
    }

    SECTION("Throw on immediate end of file")
    {
        auto conn = setup_test();
        REQUIRE_THROWS_AS((void)conn.receive(), std::system_error);
    }

    SECTION("Throw on end of file after data")
    {
        auto conn = setup_test(R"({"object":true})");
        REQUIRE(conn.receive()["object"].get<bool>() == true);
        REQUIRE_THROWS_AS((void)conn.receive(), std::system_error);
    }
}

TEST_CASE("JSON transport async read")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    auto setup_test = [&](auto&&... args) -> test_connection {
        (socket.setup_fake(args), ...);
        return test_connection(std::move(socket));
    };

    SECTION("Read basic JSON")
    {
        auto conn = setup_test(
            R"({"object":true})",
            R"("string")",
            R"({"int":42})",
            R"({"float":3.14})",
            R"(["array"])",
            R"(null)");
        std::vector<std::function<void(const json&)>> testers = {
            [](const json& r) { REQUIRE(r["object"].get<bool>() == true); },
            [](const json& r) { REQUIRE(r.get<std::string>() == "string"); },
            [](const json& r) { REQUIRE(r["int"].get<int>() == 42); },
            [](const json& r) { REQUIRE(r["float"].get<double>() == Approx(3.14)); },
            [](const json& r) { REQUIRE(r[0].get<std::string>() == "array"); },
            [](const json& r) { REQUIRE(r.is_null() == true); },
        };
        auto test = testers.begin();
        std::function<void(std::error_code, json)> read_handler = [&](auto ec, const json& r) {
            REQUIRE(not ec);
            (*test++)(r);
            if (test != testers.end()) conn.async_receive(read_handler);
        };
        conn.async_receive(read_handler);
        REQUIRE(test == testers.begin());
        REQUIRE(ctx.run() > 0);
        REQUIRE(test == testers.end());
    }

    SECTION("Allow partial transmissions")
    {
        socket.write_max = 10;
        auto conn = setup_test(R"({"object":true})");
        bool flag{false};
        conn.async_receive([&](auto ec, const json& r) {
            REQUIRE(not ec);
            REQUIRE(r["object"].get<bool>() == true);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on partial json")
    {
        auto conn = setup_test(R"({"object":)");
        bool flag{false};
        conn.async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::invalid_argument);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on trailing bytes")
    {
        auto conn = setup_test(R"({"object":true}trailing)");
        bool flag{false};
        conn.async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::invalid_argument);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on immediate end of file")
    {
        auto conn = setup_test();
        bool flag{false};
        conn.async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::eof);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on end of file after data")
    {
        auto conn = setup_test(R"({"object":true})");
        int flag{0};
        conn.async_receive([&flag](auto ec, const json& j) {
            REQUIRE(not ec);
            REQUIRE(flag == 0);
            flag = 1;
            REQUIRE(j["object"].get<bool>() == true);
        });
        conn.async_receive([&flag](auto ec, const json&) {
            REQUIRE(ec == net::error::eof);
            REQUIRE(flag == 1);
            flag = 2;
        });
        REQUIRE(flag == 0);
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag == 2);
    }
}

TEST_CASE("JSON transport sync write")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    auto setup_test = [&](auto&&... args) -> test_connection {
        (socket.expect(args), ...);
        return test_connection(std::move(socket));
    };

    SECTION("Write basic json")
    {
        socket.validate = true;
        auto conn = setup_test("{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
        conn.send(R"({"object":true})"_json);
        conn.send(R"("string")"_json);
        conn.send(R"({"float":3.14})"_json);
        conn.send(R"(null)"_json);
    }

    SECTION("Write partial")
    {
        socket.write_max = 10;
        socket.validate = true;
        auto conn = setup_test(R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
        conn.send(R"({"s":1})"_json);
        conn.send(R"({"object":true,"toolong":true})"_json);
        conn.send(R"({"last":true})"_json);
    }

    SECTION("Write to broken pipe")
    {
        socket.error_on_write = true;
        auto conn = setup_test();
        REQUIRE_THROWS_AS(conn.send(R"({"s":1})"_json), std::system_error);
    }
}

TEST_CASE("JSON transport async write")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    auto setup_test = [&](auto&&... args) -> test_connection {
        (socket.expect(args), ...);
        return test_connection(std::move(socket));
    };

    SECTION("Write basic json")
    {
        socket.validate = true;
        auto conn = setup_test("{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
        std::vector<json> tests = {
            R"({"object":true})"_json,
            R"("string")"_json,
            R"({"float":3.14})"_json,
            R"(null)"_json,
        };
        auto test = tests.begin();
        std::function<void(std::error_code)> write_handler = [&](std::error_code ec) {
            REQUIRE(not ec);
            if (test != tests.end()) conn.async_send(*test++, write_handler);
        };
        conn.async_send(*test++, write_handler);
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Write partial")
    {
        socket.write_max = 10;
        socket.validate = true;
        auto conn = setup_test(R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
        std::vector<json> tests = {
            R"({"s":1})"_json,
            R"({"object":true,"toolong":true})"_json,
            R"({"last":true})"_json,
        };
        auto test = tests.begin();
        std::function<void(std::error_code)> write_handler = [&](std::error_code ec) {
            REQUIRE(not ec);
            if (test != tests.end()) conn.async_send(*test++, write_handler);
        };
        conn.async_send(*test++, write_handler);
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Write to broken pipe")
    {
        socket.error_on_write = true;
        auto conn = setup_test();
        bool flag{false};
        auto msg = R"({"object":true,"toolong":true})"_json;
        conn.async_send(msg, [&](auto ec) {
            REQUIRE(ec == net::error::broken_pipe);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }
}

TEST_CASE("json_connection: Executor correctness")
{
    net::io_context ctx;
    net::thread_pool tp(1);

    std::promise<std::thread::id> p_tid;
    auto f_tid = p_tid.get_future();
    post(tp, [&p_tid]() { p_tid.set_value(std::this_thread::get_id()); });
    auto this_tid = std::this_thread::get_id();
    auto pool_tid = f_tid.get();

    auto sock = FakeSocket(ctx);

    p_tid = {};
    f_tid = p_tid.get_future();

    SECTION("Send: no binding")
    {
        auto conn = test_connection(std::move(sock));
        conn.async_send({}, [&](auto ec) {
            REQUIRE(not ec);
            p_tid.set_value(std::this_thread::get_id());
        });
        ctx.run();
        REQUIRE(f_tid.get() == this_tid);
    }

    SECTION("Send: bind threadpool")
    {
        auto conn = test_connection(std::move(sock));
        conn.async_send({}, bind_executor(tp, [&](auto ec) {
                            REQUIRE(not ec);
                            p_tid.set_value(std::this_thread::get_id());
                        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }

    SECTION("Send: bind threadpool (error)")
    {
        sock.error_on_write = true;
        auto conn = test_connection(std::move(sock));
        conn.async_send({}, bind_executor(tp, [&](auto ec) {
                            REQUIRE(ec);
                            p_tid.set_value(std::this_thread::get_id());
                        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }

    SECTION("Receive: no binding")
    {
        sock.setup_fake(R"({})");
        auto conn = test_connection(std::move(sock));

        conn.async_receive([&](auto ec, const json&) {
            REQUIRE(not ec);
            p_tid.set_value(std::this_thread::get_id());
        });
        ctx.run();
        REQUIRE(f_tid.get() == this_tid);
    }

    SECTION("Receive: bind threadpool")
    {
        sock.setup_fake(R"({})");
        auto conn = test_connection(std::move(sock));

        conn.async_receive(bind_executor(tp, [&](auto ec, const json&) {
            REQUIRE(not ec);
            p_tid.set_value(std::this_thread::get_id());
        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }

    SECTION("Receive: bind threadpool (error)")
    {
        auto conn = test_connection(std::move(sock));

        conn.async_receive(bind_executor(tp, [&](auto ec, const json&) {
            REQUIRE(ec);
            p_tid.set_value(std::this_thread::get_id());
        }));
        ctx.run();
        REQUIRE(f_tid.get() == pool_tid);
    }
}
