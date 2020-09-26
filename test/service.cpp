#include <varlink/varlink.hpp>
#include <gtest/gtest.h>

using namespace varlink;
using std::string;

static constexpr std::string_view org_test_varlink = R"INTERFACE(
interface org.test
method Test(ping: string) -> (pong: string)
)INTERFACE";

class ServiceTest : public ::testing::Test {
protected:
    json testcall(std::string_view method, const json& parameters = json::object(),
                         bool more = false, bool oneway = false, const SendMore& sendmore = nullptr) {
        return service.messageCall(Message({{"method", method}, {"parameters", parameters}, {"more", more},
                                            {"oneway", oneway}}), sendmore);
    }
    Service service{"test", "unit", "1", "http://example.org"};
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

TEST_F(ServiceTest, AddInterfaceList) {
    service.addInterface(Interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInfo")["parameters"];
    EXPECT_EQ(info["interfaces"].size(), 2);
    EXPECT_EQ(info["interfaces"][1].get<string>(), "org.test");
}

TEST_F(ServiceTest, GetInterfaceDescription) {
    service.addInterface(Interface(org_test_varlink));
    auto info = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}})["parameters"];
    EXPECT_EQ(info["description"].get<string>(), "interface org.test\n\nmethod Test(ping: string) -> (pong: string)\n");
}

TEST_F(ServiceTest, GetInterfaceDescriptionNotFound) {
    auto err = testcall("org.varlink.service.GetInterfaceDescription", {{"interface", "org.test"}});
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.InterfaceNotFound");
    EXPECT_EQ(err["parameters"]["interface"].get<string>(), "org.test");
}

TEST_F(ServiceTest, AddInterfaceCall) {
    service.addInterface(Interface(org_test_varlink, {{"Test", []VarlinkCallback{
        return {{"pong", parameters["ping"].get<string>()}};
    }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}})["parameters"];
    EXPECT_EQ(pong["pong"].get<string>(), "123");
}

TEST_F(ServiceTest, AddInterfaceCallOneway) {
    service.addInterface(Interface(org_test_varlink, {{"Test", []VarlinkCallback{
        return {{"pong", parameters["ping"].get<string>()}};
    }}}));
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, false, true);
    EXPECT_EQ(pong, nullptr);
}

TEST_F(ServiceTest, AddInterfaceCallMore) {
    service.addInterface(Interface(org_test_varlink, {{"Test", []VarlinkCallback{
        sendmore({{"pong", parameters["ping"]}});
        return {{"pong", parameters["ping"]}};
    }}}));
    std::string more_reply;
    auto testmore = [&more_reply](const json &params) { more_reply = params["pong"].get<string>(); };
    auto pong = testcall("org.test.Test", {{"ping", "123"}}, true, false, testmore);
    EXPECT_EQ(more_reply, "123");
    EXPECT_EQ(pong["parameters"]["pong"].get<string>(), "123");
    EXPECT_FALSE(pong["continues"].get<bool>());
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
    service.addInterface(Interface(org_test_varlink));
    auto err = testcall("org.test.Test", {{"ping", ""}});
    EXPECT_EQ(err["error"].get<string>(), "org.varlink.service.MethodNotImplemented");
    EXPECT_EQ(err["parameters"]["method"].get<string>(), "org.test.Test");
}
