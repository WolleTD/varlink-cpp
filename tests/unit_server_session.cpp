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
             [](const auto& parameters, auto mode, const auto& send_reply) {
                 if (mode == callmode::more)
                     send_reply({}, {{"pong", parameters["ping"]}}, [=](auto) {
                         send_reply({}, {{"pong", parameters["ping"]}}, nullptr);
                     });
                 else
                     send_reply({}, {{"pong", parameters["ping"]}}, nullptr);
             }},
            {"TestTypes",
             [](const auto&, auto) -> json {
                 return {{"pong", 123}};
             }},
            {"VarlinkError",
             [](const auto&, auto) -> json { throw varlink_error("org.test.Error", json::object()); }},
            {"Exception", [](const auto&, auto) -> json { throw std::exception(); }},
        });

    std::exception_ptr ex_ptr;
    int ex_handler_called = 0;
    int expected_calls = 1;
    auto ex_handler = [&](const std::exception_ptr& eptr) {
        if (ex_handler_called++ == 0) ex_ptr = eptr;
    };
    std::string expected_message = "End of file";

    auto setup_test = [&](const auto& call, const auto& expected_response) {
        socket.setup_fake(call);
        socket.expect(expected_response);
        conn = std::make_shared<test_session>(std::move(socket), service, ex_handler);
    };

    SECTION("Simple ping call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"}})",
            R"({"parameters":{"pong":"123"}})");
    }

    SECTION("Ping call with wrong type")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":123}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"ping"}})");
    }

    SECTION("Ping call with wrong response")
    {
        socket.validate = true;
        // One for the exception, one for EOF
        expected_calls = 2;
        expected_message = "pong";
        setup_test(
            R"({"method":"org.test.TestTypes","parameters":{"ping":"123"}})",
            R"({"error":"org.varlink.service.InvalidParameter","parameters":{"parameter":"pong"}})");
    }

    SECTION("More call")
    {
        socket.validate = true;
        std::string resp = R"({"continues":true,"parameters":{"pong":"123"}})";
        resp += '\0';
        resp += R"({"continues":false,"parameters":{"pong":"123"}})";
        setup_test(R"({"method":"org.test.Test","parameters":{"ping":"123"},"more":true})", resp);
    }

    SECTION("Oneway call")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.Test","parameters":{"ping":"123"},"oneway":true})",
            net::buffer(&conn, 0));
    }

    SECTION("Oneway call and regular call")
    {
        socket.validate = true;
        std::string req = R"({"method":"org.test.Test","parameters":{"ping":"123"},"oneway":true})";
        req += '\0';
        req += R"({"method":"org.test.Test","parameters":{"ping":"123"}})";
        setup_test(req, R"({"parameters":{"pong":"123"}})");
    }

    SECTION("Method not found")
    {
        socket.validate = true;
        setup_test(
            R"({"method":"org.test.NotFound"})",
            R"({"error":"org.varlink.service.MethodNotFound","parameters":{"method":"org.test.NotFound"}})");
    }

    SECTION("Method not implemented")
    {
        socket.validate = true;
        // One for the exception, one for EOF
        expected_calls = 2;
        expected_message = "bad_function_call";
        setup_test(
            R"({"method":"org.test.NotImplemented"})",
            R"({"error":"org.varlink.service.MethodNotImplemented","parameters":{"method":"org.test.NotImplemented"}})");
    }

    SECTION("Method throws varlink_error")
    {
        socket.validate = true;
        // One for the exception, one for EOF
        expected_calls = 2;
        expected_message = "org.test.Error with args: {}";
        setup_test(
            R"({"method":"org.test.VarlinkError"})", R"({"error":"org.test.Error","parameters":{}})");
    }

    SECTION("Method throws std::exception")
    {
        socket.validate = true;
        // One for the exception, one for EOF
        expected_calls = 2;
        expected_message = "std::exception";
        setup_test(
            R"({"method":"org.test.Exception"})",
            R"({"error":"org.varlink.service.InternalError","parameters":{"what":"std::exception"}})");
    }

    SECTION("Send invalid varlink")
    {
        socket.validate = true;
        setup_test(R"({"dohtem":"org.test.Test"})", net::buffer(&conn, 0));
        expected_message = R"(Not a varlink message: {"dohtem":"org.test.Test"})";
    }

    SECTION("Send nothing")
    {
        socket.validate = true;
        setup_test(net::buffer(&conn, 0), net::buffer(&conn, 0));
    }

    SECTION("Write error")
    {
        socket.error_on_write = true;
        // one for write error and one for EOF, NotImplemented is masked by the write error.
        expected_calls = 2;
        expected_message = "Broken pipe";
        setup_test(R"({"method":"org.test.NotImplemented"})", "");
    }

    conn->start();
    REQUIRE(ctx.run() > 0);
    std::string msg;
    try {
        if (ex_ptr) std::rethrow_exception(ex_ptr);
    }
    catch (std::exception& e) {
        msg = e.what();
    }
    REQUIRE(msg == expected_message);
    REQUIRE(ex_handler_called == expected_calls);
}
