#include <gtest/gtest.h>

#include <varlink/varlink.hpp>

using namespace varlink;
using std::string;

static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
)INTERFACE";

class ServiceTest : public ::testing::Test {
   protected:
    json testcall(std::string_view method, const json& parameters = json::object(), bool more = false,
                  bool oneway = false, const sendmore_function& sendmore = nullptr) {
        return service.message_call(
            varlink_message({{"method", method}, {"parameters", parameters}, {"more", more}, {"oneway", oneway}}), sendmore);
    }
    varlink_service service{"test", "unit", "1", "http://example.org"};
};

TEST_F(ServiceTest, Create) {
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    EXPECT_EQ(info["vendor"].get<string>(), "test");
    EXPECT_EQ(info["product"].get<string>(), "unit");
    EXPECT_EQ(info["version"].get<string>(), "1");
    EXPECT_EQ(info["url"].get<string>(), "http://example.org");
    EXPECT_EQ(info["interfaces"].size(), 1);
    EXPECT_EQ(info["interfaces"][0].get<string>(), "org.varlink.service");
}

TEST_F(ServiceTest, SetInterfaceList) {
    service.add_interface(varlink_interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    EXPECT_EQ(info["interfaces"].size(), 2);
    EXPECT_EQ(info["interfaces"][1].get<string>(), "org.test");
}

TEST_F(ServiceTest, GetInterfaceDescription) {
    service.add_interface(varlink_interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})["parameters"];
    EXPECT_EQ(info["description"].get<string>(), "interface org.test\n\nmethod Test(ping: string) -> (pong: string)\n");
}

TEST_F(ServiceTest, GetInterfaceDescriptionNotFound) {
    auto err = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}});
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(err["parameters"]["interface"].get<string>(), "org.test");
}

TEST_F(ServiceTest, SetInterfaceDouble) {
    service.add_interface(varlink_interface(org_test_varlink));
    EXPECT_THROW(service.add_interface(varlink_interface(org_test_varlink)), std::invalid_argument);
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    EXPECT_EQ(info["interfaces"].size(), 2);
    EXPECT_EQ(info["interfaces"][1].get<string>(), "org.test");
}

TEST_F(ServiceTest, SetInterfaceCall) {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", parameters["ping"].get<string>()}};
                                                                }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}})["parameters"];
    EXPECT_EQ(pong["pong"].get<string>(), "123");
}

TEST_F(ServiceTest, SetInterfaceCallOneway) {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", parameters["ping"].get<string>()}};
                                                                }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, false, true);
    EXPECT_EQ(pong, nullptr);
}

TEST_F(ServiceTest, SetInterfaceCallMore) {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    sendmore({{"pong", parameters["ping"]}});
                                                                    return {{"pong", parameters["ping"]}};
                                                                }}}));
    std::string more_reply;
    auto testmore = [&more_reply](const json& more) { more_reply = more["parameters"]["pong"].get<string>(); };
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, true, false, testmore);
    EXPECT_EQ(more_reply, "123");
    EXPECT_EQ(pong["parameters"]["pong"].get<string>(), "123");
    EXPECT_FALSE(pong["continues"].get<bool>());
}

TEST_F(ServiceTest, SetInterfaceCallMoreNull) {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    sendmore({{"pong", parameters["ping"]}});
                                                                    return {{"pong", parameters["ping"]}};
                                                                }}}));
    std::string more_reply;
    auto err = testcall("org.test.Test", {{"ping", "123"}}, true, false, nullptr);
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.MethodNotImplemented");
}

TEST_F(ServiceTest, SetInterfaceCallError) {
    service.add_interface(
        varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                  throw varlink_error{"org.test.Error", json::object()};
                                              }}}));
    auto err = testcall("org.test.Test", {{"ping", "test"}});
    EXPECT_EQ(err["error"].get<string>(), "org.test.Error");
}

TEST_F(ServiceTest, SetInterfaceCallException) {
    service.add_interface(
        varlink_interface(org_test_varlink, {{"Test", [] varlink_callback { throw std::exception(); }}}));
    auto err = testcall("org.test.Test", {{"ping", "test"}});
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.InternalError");
    EXPECT_EQ(err["parameters"]["what"].get<string>(), "std::exception");
}

TEST_F(ServiceTest, SetInterfaceCallResponseError) {
    service.add_interface(varlink_interface(org_test_varlink, {{"Test", [] varlink_callback {
                                                                    return {{"pong", true}};
                                                                }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}})["parameters"];
    EXPECT_TRUE(pong["pong"].get<bool>());
}

TEST_F(ServiceTest, InterfaceNotFound) {
    auto err = testcall("org.test.Test");
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(err["parameters"]["interface"].get<string>(), "org.test");
}

TEST_F(ServiceTest, MethodNotFound) {
    auto err = testcall("org.varlink.service.Nonexistent");
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.MethodNotFound");
    EXPECT_EQ(err["parameters"]["method"].get<string>(), "org.varlink.service.Nonexistent");
}

TEST_F(ServiceTest, InvalidParameter) {
    auto err = testcall("org.varlink.service.GetInterfaceDescription");
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.InvalidParameter");
    EXPECT_EQ(err["parameters"]["parameter"].get<string>(), "interface");
}

TEST_F(ServiceTest, MethodNotImplemented) {
    service.add_interface(varlink_interface(org_test_varlink));
    auto err = testcall("org.test.Test", {{"ping", ""}});
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.MethodNotImplemented");
    EXPECT_EQ(err["parameters"]["method"].get<string>(), "org.test.Test");
}
