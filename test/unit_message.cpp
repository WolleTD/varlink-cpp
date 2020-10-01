#include <gtest/gtest.h>

#include <varlink/varlink.hpp>

using namespace varlink;

TEST(Message, Create) {
    EXPECT_NO_THROW(Message(R"({"method":""})"_json));
    EXPECT_NO_THROW(Message(R"({"method":"test"})"_json));
    EXPECT_NO_THROW(Message(R"({"method":"a.b.C"})"_json));
    EXPECT_NO_THROW(Message(R"({"method":"a.b.C","parameters":{}})"_json));
    EXPECT_NO_THROW(Message(R"({"method":"","parameters":{"a":1}})"_json));

    EXPECT_THROW(Message(R"({"method":42})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"method":null})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"method":[]})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"method":"","parameters":1})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"method":"","parameters":null})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"method":"","parameters":[]})"_json), std::invalid_argument);

    EXPECT_THROW(Message(R"(null)"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"(42)"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"("string")"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({})"_json), std::invalid_argument);
    EXPECT_THROW(Message(R"({"parameters":{}})"_json), std::invalid_argument);
}

TEST(Message, Parameters) {
    auto defaultParams = Message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.parameters(), json::object());
    auto withParams = Message(R"({"method":"","parameters":{"a":1}})"_json);
    EXPECT_EQ(withParams.parameters(), R"({"a":1})"_json);
}

TEST(Message, More) {
    auto defaultParams = Message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.more(), false);
    auto withMoreTrue = Message(R"({"method":"","more":true})"_json);
    EXPECT_EQ(withMoreTrue.more(), true);
    auto withMoreFalse = Message(R"({"method":"","more":false})"_json);
    EXPECT_EQ(withMoreFalse.more(), false);
}

TEST(Message, Oneway) {
    auto defaultParams = Message(R"({"method":""})"_json);
    EXPECT_EQ(defaultParams.oneway(), false);
    auto withOnewayTrue = Message(R"({"method":"","oneway":true})"_json);
    EXPECT_EQ(withOnewayTrue.oneway(), true);
    auto withOnewayFalse = Message(R"({"method":"","oneway":false})"_json);
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
        auto msg = Message(json{{"method", test.input}});
        EXPECT_EQ(msg.interfaceAndMethod(), test.output);
    }
}
