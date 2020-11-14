#include <catch2/catch.hpp>

#include <varlink/varlink.hpp>

using namespace varlink;
using std::string;

TEST_CASE("Varlink service")
{
    varlink_service service{{"test", "unit", "1", "http://example.org"}};
    auto testcall = [&service](
                        std::string_view method,
                        const json& parameters = json::object(),
                        bool more = false,
                        bool oneway = false,
                        const sendmore_function& sendmore = nullptr) {
        return service.message_call(
            varlink_message(
                {{"method", method},
                 {"parameters", parameters},
                 {"more", more},
                 {"oneway", oneway}}),
            sendmore);
    };

    SECTION("Call org.varlink.service.GetInfo")
    {
        auto info = testcall("org.varlink.service.GetInfo")["parameters"];
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
        auto info = testcall("org.varlink.service.GetInfo")["parameters"];
        REQUIRE(info["interfaces"].size() == 2);
        REQUIRE(info["interfaces"][1].get<string>() == "org.test");
    }

    SECTION("Add an interface and get it's description")
    {
        auto info = testcall(
            "org.varlink.service.GetInterfaceDescription",
            {{"interface", "org.test"}})["parameters"];
        REQUIRE(info["description"].get<string>() == "interface org.test\n\nmethod Test(ping: string) -> (pong: string)\n");
    }

    SECTION("Call GetInterfaceDescription for an unknown interface")
    {
        auto err = testcall(
            "org.varlink.service.GetInterfaceDescription",
            {{"interface", "org.not.test"}});
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
        auto info = testcall("org.varlink.service.GetInfo")["parameters"];
        REQUIRE(info["interfaces"].size() == 2);
        REQUIRE(info["interfaces"][1].get<string>() == "org.test");
    }
}

TEST_CASE("Varlink service callbacks")
{
    varlink_service service{{"test", "unit", "1", "http://example.org"}};
    auto testcall = [&service](
                        std::string_view method,
                        const json& parameters = json::object(),
                        bool more = false,
                        bool oneway = false,
                        const sendmore_function& sendmore = nullptr) {
        return service.message_call(
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
                 if (sendmore)
                     sendmore({{"pong", 123}});
                 return {{"pong", 123}};
             }},
            {"VarlinkError",
             [] varlink_callback {
                 throw varlink_error("org.test.Error", json::object());
             }},
            {"Exception", [] varlink_callback { throw std::exception(); }},
        }));

    SECTION("Call a method on an unknown interface")
    {
        auto err = testcall("org.not.test.Test");
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InterfaceNotFound");
        REQUIRE(err["parameters"]["interface"].get<string>() == "org.not.test");
    }

    SECTION("Call a unknown method on a known interface")
    {
        auto err = testcall("org.test.Nonexistent");
        REQUIRE(
            err["error"].get<string>() == "org.varlink.service.MethodNotFound");
        REQUIRE(
            err["parameters"]["method"].get<string>() == "org.test.Nonexistent");
    }

    SECTION("Call GetInterfaceDescription with missing parameters")
    {
        auto err = testcall("org.test.Test");
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "ping");
    }

    SECTION("Call a method that has no callback defined")
    {
        auto err = testcall("org.test.NotImplemented", {{"ping", ""}});
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.MethodNotImplemented");
        REQUIRE(
            err["parameters"]["method"].get<string>()
            == "org.test.NotImplemented");
    }

    SECTION("Add interface with callback and call it")
    {
        auto pong = testcall("org.test.Test", {{"ping", "123"}})["parameters"];
        REQUIRE(pong["pong"].get<string>() == "123");
    }

    SECTION("Add interface with callback and call it (oneway)")
    {
        auto pong = testcall("org.test.Test", {{"ping", "123"}}, false, true);
        REQUIRE(pong == nullptr);
    }

    SECTION("Add interface with callback and call it (more)")
    {
        std::string more_reply;
        auto testmore = [&more_reply](const json& more) {
            more_reply = more["parameters"]["pong"].get<string>();
        };
        auto pong = testcall(
            "org.test.Test", {{"ping", "123"}}, true, false, testmore);
        REQUIRE(more_reply == "123");
        REQUIRE(pong["parameters"]["pong"].get<string>() == "123");
        REQUIRE_FALSE(pong["continues"].get<bool>());
    }

    SECTION("Throw a varlink_error in the callback")
    {
        auto err = testcall("org.test.VarlinkError", {{"ping", "test"}});
        REQUIRE(err["error"].get<string>() == "org.test.Error");
    }

    SECTION("Throw std::exception in the callback")
    {
        auto err = testcall("org.test.Exception", {{"ping", "test"}});
        REQUIRE(
            err["error"].get<string>() == "org.varlink.service.InternalError");
        REQUIRE(err["parameters"]["what"].get<string>() == "std::exception");
    }

    SECTION("Varlink error on response type error")
    {
        auto err = testcall("org.test.TestTypes", {{"ping", "123"}});
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
    }

    SECTION("Varlink error on more response type error")
    {
        bool more_called = false;
        auto testmore = [&more_called](const json&) { more_called = true; };
        auto err = testcall(
            "org.test.TestTypes", {{"ping", "123"}}, true, false, testmore);
        REQUIRE_FALSE(more_called);
        REQUIRE(
            err["error"].get<string>()
            == "org.varlink.service.InvalidParameter");
        REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
    }
}
