#include <catch2/catch.hpp>

#include <varlink/service.hpp>

using namespace varlink;
using std::string;

TEST_CASE("Varlink service")
{
    varlink_service service{{"test", "unit", "1", "http://example.org"}};
    auto testcall = [&service](
                        std::string_view method,
                        const json& parameters,
                        bool more,
                        bool oneway,
                        const std::function<void(const json&, bool)>& sendmore) {
        service.message_call(
            varlink_message(
                {{"method", method},
                 {"parameters", parameters},
                 {"more", more},
                 {"oneway", oneway}}),
            sendmore);
    };

    SECTION("Call org.varlink.service.GetInfo")
    {
        json info;
        testcall(
            "org.varlink.service.GetInfo",
            json::object(),
            false,
            false,
            [&](auto&& r, bool) { info = r["parameters"]; });
        REQUIRE(info["vendor"].get<string>() == "test");
        REQUIRE(info["product"].get<string>() == "unit");
        REQUIRE(info["version"].get<string>() == "1");
        REQUIRE(info["url"].get<string>() == "http://example.org");
        REQUIRE(info["interfaces"].size() == 1);
        REQUIRE(info["interfaces"][0].get<string>() == "org.varlink.service");
    }

    static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
)INTERFACE";

    service.add_interface(varlink_interface(org_test_varlink));

    SECTION("Add an interface and call .GetInfo")
    {
        json info;
        testcall(
            "org.varlink.service.GetInfo",
            json::object(),
            false,
            false,
            [&](auto&& r, bool) { info = r["parameters"]; });
        REQUIRE(info["interfaces"].size() == 2);
        REQUIRE(info["interfaces"][1].get<string>() == "org.test");
    }

    SECTION("Add an interface and get it's description")
    {
        json info;
        testcall(
            "org.varlink.service.GetInterfaceDescription",
            {{"interface", "org.test"}},
            false,
            false,
            [&](auto&& r, bool) { info = r["parameters"]; });
        REQUIRE(info["description"].get<string>() == "interface org.test\n\nmethod Test(ping: string) -> (pong: string)\n");
    }

    SECTION("Call GetInterfaceDescription for an unknown interface")
    {
        json err;
        testcall(
            "org.varlink.service.GetInterfaceDescription",
            {{"interface", "org.not.test"}},
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InterfaceNotFound");
        REQUIRE(err["parameters"]["interface"].get<string>() == "org.not.test");
    }

    SECTION("Trying to add an interface a second time throws")
    {
        REQUIRE_THROWS_AS(
            service.add_interface(varlink_interface(org_test_varlink)),
            std::invalid_argument);
        json info;
        testcall(
            "org.varlink.service.GetInfo",
            json::object(),
            false,
            false,
            [&](auto&& r, bool) { info = r["parameters"]; });
        REQUIRE(info["interfaces"].size() == 2);
        REQUIRE(info["interfaces"][1].get<string>() == "org.test");
    }
}

TEST_CASE("Varlink service callbacks")
{
    varlink_service service{{"test", "unit", "1", "http://example.org"}};
    auto testcall = [&service](
                        std::string_view method,
                        const json& parameters,
                        bool more,
                        bool oneway,
                        const std::function<void(const json&, bool)>& sendmore) {
        service.message_call(
            varlink_message(
                {{"method", method},
                 {"parameters", parameters},
                 {"more", more},
                 {"oneway", oneway}}),
            sendmore);
    };

    static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
method TestTypes(ping: string) -> (pong: string)
method NotImplemented(ping: string) -> (pong: string)
method VarlinkError(ping: string) -> (pong: string)
method Exception(ping: string) -> (pong: string)
method EmptyReply() -> ()
)INTERFACE";

    service.add_interface(varlink_interface(
        org_test_varlink,
        {
            {"Test",
             [] varlink_callback {
                 if (wants_more)
                     send_reply({{"pong", parameters["ping"]}}, true);
                 send_reply({{"pong", parameters["ping"]}}, false);
             }},
            {"TestTypes",
             [] varlink_callback {
                 if (wants_more)
                     send_reply({{"pong", 123}}, true);
                 send_reply({{"pong", 123}}, false);
             }},
            {"VarlinkError",
             [] varlink_callback {
                 throw varlink_error("org.test.Error", json::object());
             }},
            {"Exception", [] varlink_callback { throw std::exception(); }},
            {"EmptyReply", [] varlink_callback { send_reply({}, false); }},
        }));

    SECTION("Call a method on an unknown interface")
    {
        json err;
        testcall(
            "org.not.test.Test", json::object(), false, false, [&](auto&& r, bool) {
                err = r;
            });
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InterfaceNotFound");
        REQUIRE(err["parameters"]["interface"].get<string>() == "org.not.test");
    }

    SECTION("Call a unknown method on a known interface")
    {
        json err;
        testcall(
            "org.test.Nonexistent",
            json::object(),
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(
            err["error"].get<string>() == "org.varlink.service.MethodNotFound");
        REQUIRE(
            err["parameters"]["method"].get<string>() == "org.test.Nonexistent");
    }

    SECTION("Call a method with missing parameters")
    {
        json err;
        testcall(
            "org.test.Test", json::object(), false, false, [&](auto&& r, bool) {
                err = r;
            });
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "ping");
    }

    SECTION("Call a method that has no callback defined")
    {
        json err;
        testcall(
            "org.test.NotImplemented",
            {{"ping", ""}},
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.MethodNotImplemented");
        REQUIRE(
            err["parameters"]["method"].get<string>()
            == "org.test.NotImplemented");
    }

    SECTION("Add interface with callback and call it")
    {
        json pong;
        testcall(
            "org.test.Test", {{"ping", "123"}}, false, false, [&](auto&& r, bool) {
                pong = r["parameters"];
            });
        REQUIRE(pong["pong"].get<string>() == "123");
    }

    SECTION("Correctly serialize an empty reply")
    {
        json pong;
        testcall(
            "org.test.EmptyReply",
            json::object(),
            false,
            false,
            [&](auto&& r, bool) { pong = r["parameters"]; });
        REQUIRE(pong.is_object());
        REQUIRE(pong.empty());
    }

    SECTION("Add interface with callback and call it (oneway)")
    {
        json pong = "not null";
        testcall(
            "org.test.Test", {{"ping", "123"}}, false, true, [&](auto&& r, bool) {
                pong = r;
            });
        REQUIRE(pong == nullptr);
    }

    SECTION("Add interface with callback and call it (oneway and regular)")
    {
        json pong = "not null";
        testcall(
            "org.test.Test", {{"ping", "123"}}, false, true, [&](auto&& r, bool) {
                pong = r;
            });
        REQUIRE(pong == nullptr);
        testcall(
            "org.test.Test", {{"ping", "123"}}, false, false, [&](auto&& r, bool) {
                pong = r["parameters"];
            });
        REQUIRE(pong["pong"].get<string>() == "123");
    }

    SECTION("Add interface with callback and call it (more)")
    {
        std::array<json, 2> replies;
        size_t i = 0;
        testcall(
            "org.test.Test", {{"ping", "123"}}, true, false, [&](auto&& r, bool) {
                replies[i++] = r;
            });
        REQUIRE(replies[0]["parameters"]["pong"].get<string>() == "123");
        REQUIRE(replies[1]["parameters"]["pong"].get<string>() == "123");
        REQUIRE(replies[0]["continues"].get<bool>());
        REQUIRE(not replies[1]["continues"].get<bool>());
    }

    SECTION("Throw a varlink_error in the callback")
    {
        json err;
        testcall(
            "org.test.VarlinkError",
            {{"ping", "test"}},
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(err["error"].get<string>() == "org.test.Error");
    }

    SECTION("Throw std::exception in the callback")
    {
        json err;
        testcall(
            "org.test.Exception",
            {{"ping", "test"}},
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(
            err["error"].get<string>() == "org.varlink.service.InternalError");
        REQUIRE(err["parameters"]["what"].get<string>() == "std::exception");
    }

    SECTION("Varlink error on response type error")
    {
        json err;
        testcall(
            "org.test.TestTypes",
            {{"ping", "123"}},
            false,
            false,
            [&](auto&& r, bool) { err = r; });
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
    }

    SECTION("Varlink error on more response type error")
    {
        json err;
        size_t num_called = 0;
        testcall(
            "org.test.TestTypes", {{"ping", "123"}}, true, false, [&](auto&& r, bool) {
                num_called++;
                err = r;
            });
        REQUIRE(num_called == 1);
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
    }
}
