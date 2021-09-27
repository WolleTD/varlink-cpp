#include <catch2/catch.hpp>
#include <varlink/json_connection.hpp>

#include "fake_socket.hpp"

using namespace varlink;
using test_connection = json_connection<fake_proto>;

TEST_CASE("JSON transport sync read")
{
    net::io_context ctx{};
    std::unique_ptr<test_connection> conn{};
    auto setup_test = [&](auto&&... args) {
        auto socket = FakeSocket{ctx};
        (socket.setup_fake(args), ...);
        conn = std::make_unique<test_connection>(std::move(socket));
    };

    SECTION("Read basic JSON")
    {
        setup_test(
            R"({"object":true})",
            R"("string")",
            R"({"int":42})",
            R"({"float":3.14})",
            R"(["array"])",
            R"(null)");
        REQUIRE(conn->receive()["object"].get<bool>() == true);
        REQUIRE(conn->receive().get<std::string>() == "string");
        REQUIRE(conn->receive()["int"].get<int>() == 42);
        REQUIRE(conn->receive()["float"].get<double>() == Approx(3.14));
        REQUIRE(conn->receive()[0].get<std::string>() == "array");
        REQUIRE(conn->receive().is_null() == true);
    }

    SECTION("Read partial")
    {
        setup_test(R"({"object":true})");
        conn->socket().write_max = 10;
        REQUIRE(conn->receive()["object"].get<bool>() == true);
    }

    SECTION("Throw on partial json")
    {
        setup_test(R"({"object":)");
        REQUIRE_THROWS_AS((void)conn->receive(), std::invalid_argument);
    }

    SECTION("Throw on trailing bytes")
    {
        setup_test(R"({"object":true}trailing)");
        REQUIRE_THROWS_AS((void)conn->receive(), std::invalid_argument);
    }

    SECTION("Throw on immediate end of file")
    {
        setup_test();
        REQUIRE_THROWS_AS((void)conn->receive(), std::system_error);
    }

    SECTION("Throw on end of file after data")
    {
        setup_test(R"({"object":true})");
        REQUIRE(conn->receive()["object"].get<bool>() == true);
        REQUIRE_THROWS_AS((void)conn->receive(), std::system_error);
    }
}

TEST_CASE("JSON transport async read")
{
    net::io_context ctx{};
    std::unique_ptr<test_connection> conn{};
    auto setup_test = [&](auto&&... args) {
        auto socket = FakeSocket{ctx};
        (socket.setup_fake(args), ...);
        conn = std::make_unique<test_connection>(std::move(socket));
    };

    SECTION("Read basic JSON")
    {
        setup_test(
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
            [](const json& r) {
                REQUIRE(r["float"].get<double>() == Approx(3.14));
            },
            [](const json& r) { REQUIRE(r[0].get<std::string>() == "array"); },
            [](const json& r) { REQUIRE(r.is_null() == true); },
        };
        auto test = testers.begin();
        conn->async_receive([&test](auto ec, const json& r) {
            REQUIRE(not ec);
            (*test++)(r);
        });
        REQUIRE(test == testers.begin());
        REQUIRE(ctx.run() > 0);
        REQUIRE(test == testers.end());
    }

    SECTION("Allow partial transmissions")
    {
        setup_test(R"({"object":true})");
        conn->socket().write_max = 10;
        bool flag{false};
        conn->async_receive([&](auto ec, const json& r) {
            REQUIRE(not ec);
            REQUIRE(r["object"].get<bool>() == true);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on partial json")
    {
        setup_test(R"({"object":)");
        bool flag{false};
        conn->async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::invalid_argument);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on trailing bytes")
    {
        setup_test(R"({"object":true}trailing)");
        bool flag{false};
        conn->async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::invalid_argument);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on immediate end of file")
    {
        setup_test();
        bool flag{false};
        conn->async_receive([&](auto ec, auto) {
            REQUIRE(ec == net::error::eof);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }

    SECTION("Throw on end of file after data")
    {
        setup_test(R"({"object":true})");
        int flag{0};
        conn->async_receive([&flag](auto ec, const json& j) {
            REQUIRE(not ec);
            REQUIRE(flag == 0);
            flag = 1;
            REQUIRE(j["object"].get<bool>() == true);
        });
        conn->async_receive([&flag](auto ec, const json&) {
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
    std::unique_ptr<test_connection> conn{};
    auto setup_test = [&](auto&&... args) {
        auto socket = FakeSocket{ctx};
        (socket.expect(args), ...);
        conn = std::make_unique<test_connection>(std::move(socket));
    };

    SECTION("Write basic json")
    {
        setup_test(
            "{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
        conn->send(R"({"object":true})"_json);
        conn->send(R"("string")"_json);
        conn->send(R"({"float":3.14})"_json);
        conn->send(R"(null)"_json);
        conn->socket().validate_write();
    }

    SECTION("Write partial")
    {
        setup_test(
            R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
        conn->socket().write_max = 10;
        conn->send(R"({"s":1})"_json);
        conn->send(R"({"object":true,"toolong":true})"_json);
        conn->send(R"({"last":true})"_json);
        conn->socket().validate_write();
    }

    SECTION("Write to broken pipe")
    {
        setup_test();
        conn->socket().error_on_write = true;
        REQUIRE_THROWS_AS(conn->send(R"({"s":1})"_json), std::system_error);
    }
}

TEST_CASE("JSON transport async write")
{
    net::io_context ctx{};
    std::unique_ptr<test_connection> conn{};
    auto setup_test = [&](auto&&... args) {
        auto socket = FakeSocket{ctx};
        (socket.expect(args), ...);
        conn = std::make_unique<test_connection>(std::move(socket));
    };

    SECTION("Write basic json")
    {
        setup_test(
            "{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
        std::vector<json> tests = {
            R"({"object":true})"_json,
            R"("string")"_json,
            R"({"float":3.14})"_json,
            R"(null)"_json,
        };
        for (const auto& t : tests) {
            conn->async_send(t, [](auto ec) { REQUIRE(not ec); });
        }
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Write partial")
    {
        setup_test(
            R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
        conn->socket().write_max = 10;
        std::vector<json> tests = {
            R"({"s":1})"_json,
            R"({"object":true,"toolong":true})"_json,
            R"({"last":true})"_json,
        };
        for (const auto& t : tests) {
            conn->async_send(t, [](auto ec) { REQUIRE(not ec); });
        }
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Write to broken pipe")
    {
        setup_test();
        bool flag{false};
        conn->socket().error_on_write = true;
        auto msg = R"({"object":true,"toolong":true})"_json;
        conn->async_send(msg, [&](auto ec) {
            REQUIRE(ec == net::error::broken_pipe);
            flag = true;
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(flag);
    }
}
