#include <catch2/catch.hpp>
#include <varlink/server.hpp>

#include "fake_socket.hpp"

using namespace varlink;
using test_session = server_session<FakeSocket>;

TEST_CASE("Server session processing")
{
    net::io_context ctx{};
    auto socket = FakeSocket{ctx};
    std::shared_ptr<test_session> conn{};
    varlink_service service{{"test", "unit", "1", "http://example.org"}};

    static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
method TestTypes(ping: string) -> (pong: string)
method NotImplemented() -> ()
method VarlinkError() -> ()
method Exception() -> ()
)INTERFACE";

    service.add_interface(varlink_interface(
        org_test_varlink,
        {
            {"Test",
             [] varlink_callback {
                 if (sendmore)
                     sendmore({{"pong", parameters["ping"]}});
                 return {{"pong", parameters["ping"]}};
             }},
            {"TestTypes",
             [] varlink_callback {
                 return {{"pong", 123}};
             }},
            {"VarlinkError",
             [] varlink_callback {
                 throw varlink_error("org.test.Error", json::object());
             }},
            {"Exception", [] varlink_callback { throw std::exception(); }},
        }));

    auto setup_test = [&](const auto& call, const auto& expected_response) {
        socket.setup_fake(call);
        socket.expect(expected_response);
        conn = std::make_shared<test_session>(std::move(socket), service);
    };

    SECTION("Simple ping call")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Ping call with wrong type")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":123}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"ping"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Ping call with wrong response")
    {
        setup_test(
            R"({"method":"org.test.TestTypes","parameters":{"ping":"123"}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"pong"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("More call")
    {
        std::string resp = R"({"continues":true,"parameters":{"pong":"123"}})";
        resp += '\0';
        resp += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"more":true})",
            resp);
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Oneway call")
    {
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"oneway":true})",
            asio::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Method not found")
    {
        setup_test(
            R"({"method":"org.test.NotFound"})",
            R"({"error":"org.varlink.service.MethodNotFound","parameters":{"method":"org.test.NotFound"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Method not implemented")
    {
        setup_test(
            R"({"method":"org.test.NotImplemented"})",
            R"({"error":"org.varlink.service.MethodNotImplemented","parameters":{"method":"org.test.NotImplemented"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Method throws varlink_error")
    {
        setup_test(
            R"({"method":"org.test.VarlinkError"})",
            R"({"error":"org.test.Error","parameters":{}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Method throws std::exception")
    {
        setup_test(
            R"({"method":"org.test.Exception"})",
            R"({"error":"org.varlink.service.InternalError","parameters":{"what":"std::exception"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Send invalid varlink")
    {
        setup_test(R"({"dohtem":"org.test.Test"})", net::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }

    SECTION("Send nothing")
    {
        setup_test(net::buffer(&conn, 0), net::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
        conn->socket().validate_write();
    }
}
