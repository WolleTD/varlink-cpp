#include <catch2/catch_test_macros.hpp>
#include <varlink/server_session.hpp>

#include "fake_socket.hpp"

using namespace varlink;
using test_session = server_session<fake_proto>;

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

    service.add_interface(
        org_test_varlink,
        {
            {"Test",
             [] varlink_callback {
                 if (mode == callmode::more) send_reply({{"pong", parameters["ping"]}}, true);
                 send_reply({{"pong", parameters["ping"]}}, false);
             }},
            {"TestTypes",
             [] varlink_callback {
                 send_reply({{"pong", 123}}, false);
             }},
            {"VarlinkError",
             [] varlink_callback { throw varlink_error("org.test.Error", json::object()); }},
            {"Exception", [] varlink_callback { throw std::exception(); }},
        });

    auto setup_test = [&](const auto& call, const auto& expected_response) {
        socket.setup_fake(call);
        socket.expect(expected_response);
        conn = std::make_shared<test_session>(std::move(socket), service);
    };

    SECTION("Simple ping call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Ping call with wrong type")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":123}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"ping"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Ping call with wrong response")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.TestTypes","parameters":{"ping":"123"}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"pong"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("More call")
    {
        socket.validate = true;
        std::string resp = R"({"continues":true,"parameters":{"pong":"123"}})";
        resp += '\0';
        resp += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"more":true})",
            resp);
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Oneway call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"oneway":true})",
            asio::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Oneway call and regular call")
    {
        socket.validate = true;
        std::string req =
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"oneway":true})";
        req += '\0';
        req += R"({"method":"org.test.Test","parameters":{"ping":"123"}})";
        setup_test(req, R"({"parameters":{"pong":"123"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Method not found")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.NotFound"})",
            R"({"error":"org.varlink.service.MethodNotFound","parameters":{"method":"org.test.NotFound"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Method not implemented")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.NotImplemented"})",
            R"({"error":"org.varlink.service.MethodNotImplemented","parameters":{"method":"org.test.NotImplemented"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Method throws varlink_error")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.VarlinkError"})", R"({"error":"org.test.Error","parameters":{}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Method throws std::exception")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Exception"})",
            R"({"error":"org.varlink.service.InternalError","parameters":{"what":"std::exception"}})");
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Send invalid varlink")
    {
        socket.validate = true;
        setup_test(R"({"dohtem":"org.test.Test"})", net::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Send nothing")
    {
        socket.validate = true;
        setup_test(net::buffer(&conn, 0), net::buffer(&conn, 0));
        conn->start();
        REQUIRE(ctx.run() > 0);
    }

    SECTION("Write error")
    {
        socket.error_on_write = true;
        setup_test(R"({"method":"org.test.NotImplemented"})", "");
        conn->start();
        REQUIRE(ctx.run() > 0);
        // REQUIRE(conn->socket().cancelled);
    }
}
