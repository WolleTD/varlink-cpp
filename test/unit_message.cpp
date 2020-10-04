#include <gtest/gtest.h>

#include <varlink/varlink.hpp>

using namespace varlink;

TEST(Message, Create) {
    EXPECT_NO_THROW(varlink_message(R"({"method":""})"_json));
    EXPECT_NO_THROW(varlink_message(R"({"method":"test"})"_json));
    EXPECT_NO_THROW(varlink_message(R"({"method":"a.b.C"})"_json));
    EXPECT_NO_THROW(varlink_message(R"({"method":"a.b.C","parameters":{}})"_json));
    EXPECT_NO_THROW(varlink_message(R"({"method":"","parameters":{"a":1}})"_json));

    EXPECT_THROW(varlink_message(R"({"method":42})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"method":null})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"method":[]})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"method":"","parameters":1})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"method":"","parameters":null})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"method":"","parameters":[]})"_json), std::invalid_argument);

    EXPECT_THROW(varlink_message(R"(null)"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"(42)"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"("string")"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({})"_json), std::invalid_argument);
    EXPECT_THROW(varlink_message(R"({"parameters":{}})"_json), std::invalid_argument);
}

TEST(Message, Parameters) {
    auto defaultParams = varlink_message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.parameters(), json::object());
    auto withParams = varlink_message(R"({"method":"","parameters":{"a":1}})"_json);
    EXPECT_EQ(withParams.parameters(), R"({"a":1})"_json);
}

TEST(Message, More) {
    auto defaultParams = varlink_message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.more(), false);
    auto withMoreTrue = varlink_message(R"({"method":"","more":true})"_json);
    EXPECT_EQ(withMoreTrue.more(), true);
    auto withMoreFalse = varlink_message(R"({"method":"","more":false})"_json);
    EXPECT_EQ(withMoreFalse.more(), false);
}

TEST(Message, Oneway) {
    auto defaultParams = varlink_message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.oneway(), false);
    auto withOnewayTrue = varlink_message(R"({"method":"","oneway":true})"_json);
    EXPECT_EQ(withOnewayTrue.oneway(), true);
    auto withOnewayFalse = varlink_message(R"({"method":"","oneway":false})"_json);
    EXPECT_EQ(withOnewayFalse.oneway(), false);
}

TEST(Message, InterfaceAndMethod) {
    struct TestData {
        std::string input;
        std::pair<std::string, std::string> output;
    };
    const std::vector<TestData> testdata{
        {"", {"", ""}},
        {"test", {"test", "test"}},
        {"a.b", {"a", "b"}},
        {"a.b.c.D", {"a.b.c", "D"}},
        {"org.varlink.service.GetInfo", {"org.varlink.service", "GetInfo"}},
        {"a.b.c.", {"a.b.c", ""}},
    };
    for (const auto& test : testdata) {
        auto msg = varlink_message(json{{"method", test.input}});
        EXPECT_EQ(msg.interface_and_method(), test.output);
    }
}
