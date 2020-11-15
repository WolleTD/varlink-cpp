#include <catch2/catch.hpp>
#include <varlink/client.hpp>

#include "fake_socket.hpp"

using namespace varlink;
using test_client = basic_varlink_client<FakeSocket>;

TEST_CASE("Client sync call processing")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    std::shared_ptr<test_client> client{};

    auto setup_test = [&](const auto& expected_call, const auto& response) {
        socket.setup_fake(response);
        socket.expect(expected_call);
        client = std::make_shared<test_client>(std::move(socket));
    };

    SECTION("Simple call")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        auto resp = client->call(
            varlink_message("org.test.Test", {{"ping", "123"}}));
        client->socket().validate_write();
        REQUIRE(resp()["pong"].get<std::string>() == "123");
        REQUIRE(resp() == nullptr);
    }

    SECTION("Oneway call")
    {
        setup_test(
            R"({"method":"org.test.Test","oneway":true,"parameters":{"ping":"123"}})",
            net::buffer(&client, 0));
        auto resp = client->call(varlink_message(
            "org.test.Test", {{"ping", "123"}}, callmode::oneway));
        client->socket().validate_write();
        REQUIRE(resp() == nullptr);
    }

    SECTION("More call")
    {
        std::string reply = R"({"continues":true,"parameters":{"pong":"123"}})";
        reply += '\0';
        reply += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(
            R"({"method":"org.test.Test","more":true,"parameters":{"ping":"123"}})",
            reply);
        auto resp = client->call(varlink_message(
            "org.test.Test", {{"ping", "123"}}, callmode::more));
        client->socket().validate_write();
        REQUIRE(resp()["pong"].get<std::string>() == "123");
        REQUIRE(resp()["pong"].get<std::string>() == "123");
        REQUIRE(resp() == nullptr);
    }

    SECTION("Error returned")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"error":"org.test.Error","parameters":{"test":1337}})");
        auto resp = client->call(
            varlink_message("org.test.Test", {{"ping", "123"}}));
        client->socket().validate_write();
        try {
            auto j = resp();
            REQUIRE(false);
        }
        catch (varlink_error& e) {
            REQUIRE(std::string_view(e.what()) == "org.test.Error");
            REQUIRE(e.args()["test"].get<int>() == 1337);
        }
        REQUIRE(resp() == nullptr);
    }

    SECTION("End of file")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            net::buffer(std::string_view(R"({"parameters":{"pong":)")));
        auto resp = client->call(
            varlink_message("org.test.Test", {{"ping", "123"}}));
        client->socket().validate_write();
        try {
            auto j = resp();
            REQUIRE(false);
        }
        catch (std::system_error& e) {
            REQUIRE(e.code() == net::error::eof);
        }
        catch (std::exception& e) {
            // Unexpected exception
            REQUIRE(false);
        }
    }

    SECTION("Incomplete message")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":)");
        auto resp = client->call(
            varlink_message("org.test.Test", {{"ping", "123"}}));
        client->socket().validate_write();
        try {
            auto j = resp();
            REQUIRE(false);
        }
        catch (std::invalid_argument& e) {
            REQUIRE(std::string_view(e.what()) == "");
        }
    }

    SECTION("Broken pipe")
    {
        setup_test("", "");
        client->socket().error_on_write = true;
        REQUIRE_THROWS_AS(
            client->call(varlink_message("org.test.Test", {{"ping", "123"}})),
            std::system_error);
    }
}

TEST_CASE("Client async call processing")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    std::shared_ptr<test_client> client{};

    auto setup_test = [&](const auto& expected_call, const auto& response) {
        socket.setup_fake(response);
        socket.expect(expected_call);
        client = std::make_shared<test_client>(std::move(socket));
    };

    SECTION("Simple call")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto ec, const json& reply, bool more) {
            REQUIRE(not ec);
            REQUIRE(more == false);
            REQUIRE(reply["pong"].get<std::string>() == "123");
        });
        REQUIRE(ctx.run() > 0);
        client->socket().validate_write();
    }

    SECTION("Oneway call")
    {
        setup_test(
            R"({"method":"org.test.Test","oneway":true,"parameters":{"ping":"123"}})",
            net::buffer(&client, 0));
        auto msg = varlink_message(
            "org.test.Test", {{"ping", "123"}}, callmode::oneway);
        client->async_call(msg, [&](auto ec, const json& reply, bool more) {
            REQUIRE(not ec);
            REQUIRE(more == false);
            REQUIRE(reply == nullptr);
        });
        REQUIRE(ctx.run() > 0);
        client->socket().validate_write();
    }

    SECTION("More call")
    {
        std::string reply = R"({"continues":true,"parameters":{"pong":"123"}})";
        reply += '\0';
        reply += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(
            R"({"method":"org.test.Test","more":true,"parameters":{"ping":"123"}})",
            reply);
        bool was_more{false};
        auto msg = varlink_message(
            "org.test.Test", {{"ping", "123"}}, callmode::more);
        client->async_call(msg, [&](auto ec, const json& r, bool more) {
            if (not ec) {
                REQUIRE(was_more == not more);
                was_more = more;
                REQUIRE(r["pong"].get<std::string>() == "123");
            }
        });
        REQUIRE(ctx.run() > 0);
        REQUIRE(not was_more);
        client->socket().validate_write();
    }

    SECTION("Error returned")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"error":"org.test.Error","parameters":{"test":1337}})");
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto ec, const json& r, bool more) {
            if (ec == net::error::no_data) {
                REQUIRE(r["test"].get<int>() == 1337);
                REQUIRE(not more);
            }
        });
        REQUIRE(ctx.run() > 0);
        client->socket().validate_write();
    }

    SECTION("End of file")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            net::buffer(std::string_view(R"({"parameters":{"pong":)")));
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto ec, const json& r, bool more) {
            REQUIRE(ec == net::error::eof);
            REQUIRE(r == nullptr);
            REQUIRE(not more);
        });
        REQUIRE(ctx.run() > 0);
        client->socket().validate_write();
    }

    SECTION("Incomplete message")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":)");
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto ec, const json& r, bool more) {
            REQUIRE(ec == net::error::invalid_argument);
            REQUIRE(r == nullptr);
            REQUIRE(not more);
        });
        REQUIRE(ctx.run() > 0);
        client->socket().validate_write();
    }

    SECTION("Broken pipe")
    {
        setup_test("", "");
        client->socket().error_on_write = true;
        auto msg = varlink_message("org.test.Test", {{"ping", "123"}});
        client->async_call(msg, [&](auto ec, const json& r, bool more) {
            REQUIRE(ec == net::error::broken_pipe);
            REQUIRE(r == nullptr);
            REQUIRE(not more);
        });
        REQUIRE(ctx.run() > 0);
    }
}
