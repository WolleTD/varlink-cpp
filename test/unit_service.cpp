#include <catch2/catch.hpp>

#include <varlink/varlink.hpp>

using namespace varlink;
using std::string;

static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
)INTERFACE";

class ServiceTest {
   protected:
    json testcall(std::string_view method, const json& parameters = json::object(), bool more = false,
                  bool oneway = false, const sendmore_function& sendmore = nullptr) {
        return service.message_call(
            varlink_message({{"method", method}, {"parameters", parameters}, {"more", more}, {"oneway", oneway}}),
            sendmore);
    }
    varlink_service service{{"test", "unit", "1", "http://example.org"}};
};

TEST_CASE_METHOD(ServiceTest, "ServiceTest, Create") {
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    REQUIRE(info["vendor"].get<string>() == "test");
    REQUIRE(info["product"].get<string>() == "unit");
    REQUIRE(info["version"].get<string>() == "1");
    REQUIRE(info["url"].get<string>() == "http://example.org");
    REQUIRE(info["interfaces"].size() == 1);
    REQUIRE(info["interfaces"][0].get<string>() == "org.varlink.service");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceList") {
    service.add_interface(varlink_interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    REQUIRE(info["interfaces"].size() == 2);
    REQUIRE(info["interfaces"][1].get<string>() == "org.test");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, GetInterfaceDescription") {
    service.add_interface(varlink_interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})["parameters"];
    REQUIRE(info["description"].get<string>() == "interface org.test\n\nmethod Test(ping: string) -> (pong: string)\n");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, GetInterfaceDescriptionNotFound") {
    auto err = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}});
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InterfaceNotFound");
    REQUIRE(err["parameters"]["interface"].get<string>() == "org.test");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceDouble") {
    service.add_interface(varlink_interface(org_test_varlink));
    REQUIRE_THROWS_AS(service.add_interface(varlink_interface(org_test_varlink)), std::invalid_argument);
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    REQUIRE(info["interfaces"].size() == 2);
    REQUIRE(info["interfaces"][1].get<string>() == "org.test");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCall") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", parameters["ping"].get<string>()}};
                                                                }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}})["parameters"];
    REQUIRE(pong["pong"].get<string>() == "123");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallOneway") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", parameters["ping"].get<string>()}};
                                                                }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, false, true);
    REQUIRE(pong == nullptr);
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallMore") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    sendmore({{"pong", parameters["ping"]}});
                                                                    return {{"pong", parameters["ping"]}};
                                                                }}}));
    std::string more_reply;
    auto testmore = [&more_reply](const json& more) { more_reply = more["parameters"]["pong"].get<string>(); };
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, true, false, testmore);
    REQUIRE(more_reply == "123");
    REQUIRE(pong["parameters"]["pong"].get<string>() == "123");
    REQUIRE_FALSE(pong["continues"].get<bool>());
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallMoreNull") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    sendmore({{"pong", parameters["ping"]}});
                                                                    return {{"pong", parameters["ping"]}};
                                                                }}}));
    std::string more_reply;
    auto err = testcall("org.test.Test", {{"ping", "123"}}, true, false, nullptr);
    REQUIRE(err["error"].get<string>() == "org.varlink.service.MethodNotImplemented");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallError") {
    service.add_interface(
        varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                  throw varlink_error{"org.test.Error", json::object()};
                                              }}}));
    auto err = testcall("org.test.Test", {{"ping", "test"}});
    REQUIRE(err["error"].get<string>() == "org.test.Error");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallException") {
    service.add_interface(
        varlink_interface(org_test_varlink, {{"Test", [] varlink_callback { throw std::exception(); }}}));
    auto err = testcall("org.test.Test", {{"ping", "test"}});
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InternalError");
    REQUIRE(err["parameters"]["what"].get<string>() == "std::exception");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceCallResponseError") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", true}};
                                                                }}}));
    auto err = testcall("org.test.Test", {{"ping", "123"}});
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InvalidParameter");
    REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, SetInterfaceMoreResponseError") {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
      sendmore({{"pong", 123}});
      return {{"pong", "123"}};
    }}}));
    bool more_called = false;
    auto testmore = [&more_called](const json&) { more_called = true; };
    auto err = testcall("org.test.Test", {{"ping", "123"}}, true, false, testmore);
    REQUIRE_FALSE(more_called);
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InvalidParameter");
    REQUIRE(err["parameters"]["parameter"].get<string>() == "pong");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, InterfaceNotFound") {
    auto err = testcall("org.test.Test");
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InterfaceNotFound");
    REQUIRE(err["parameters"]["interface"].get<string>() == "org.test");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, MethodNotFound") {
    auto err = testcall("org.varlink.service.Nonexistent");
    REQUIRE(err["error"].get<string>() == "org.varlink.service.MethodNotFound");
    REQUIRE(err["parameters"]["method"].get<string>() == "org.varlink.service.Nonexistent");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, InvalidParameter") {
    auto err = testcall("org.varlink.service.GetInterfaceDescription");
    REQUIRE(err["error"].get<string>() == "org.varlink.service.InvalidParameter");
    REQUIRE(err["parameters"]["parameter"].get<string>() == "interface");
}

TEST_CASE_METHOD(ServiceTest, "ServiceTest, MethodNotImplemented") {
    service.add_interface(varlink_interface(org_test_varlink));
    auto err = testcall("org.test.Test", {{"ping", ""}});
    REQUIRE(err["error"].get<string>() == "org.varlink.service.MethodNotImplemented");
    REQUIRE(err["parameters"]["method"].get<string>() == "org.test.Test");
}
