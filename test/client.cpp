#include <varlink/client.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace varlink;
using ::testing::Return;

class MockConnection {
public:
    MOCK_METHOD(void, send, (const json& message));
    MOCK_METHOD(json, receive, ());
};

TEST(Client, Simple) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test"})"_json));
    EXPECT_CALL(*conn, receive()).WillOnce(Return(R"({"test":1})"_json));
    BasicClient client(std::move(conn));
    auto recv = client.call("test", {});
    EXPECT_EQ(R"({"test":1})"_json, recv());
    EXPECT_EQ(json{}, recv());
}

TEST(Client, Parameters) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test","parameters":{"test":123}})"_json));
    EXPECT_CALL(*conn, receive()).WillOnce(Return(R"({"test":123})"_json));
    BasicClient client(std::move(conn));
    auto recv = client.call("test", {{"test",123}});
    EXPECT_EQ(R"({"test":123})"_json, recv());
    EXPECT_EQ(json{}, recv());
}

TEST(Client, InvalidParameters) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test","parameters":{"test":123}})"_json));
    EXPECT_CALL(*conn, send(R"({"method":"test"})"_json)).Times(2);
    BasicClient client(std::move(conn));
    EXPECT_NO_THROW(client.call("test", {{"test",123}}));
    EXPECT_NO_THROW(client.call("test", {}));
    EXPECT_NO_THROW(client.call("test", json::object()));
    EXPECT_THROW(client.call("test", {"not an object"}), std::invalid_argument);
    EXPECT_THROW(client.call("test", true), std::invalid_argument);
    EXPECT_THROW(client.call("test", 42), std::invalid_argument);
}

TEST(Client, Oneway) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test","oneway":true})"_json));
    BasicClient client(std::move(conn));
    auto recv = client.call("test", {}, BasicClient<MockConnection>::CallMode::Oneway);
    EXPECT_EQ(json{}, recv());
}

TEST(Client, More) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test","more":true})"_json));
    EXPECT_CALL(*conn, receive()).Times(4)
            .WillOnce(Return(R"({"test":3,"continues":true})"_json))
            .WillOnce(Return(R"({"test":2,"continues":true})"_json))
            .WillOnce(Return(R"({"test":1,"continues":true})"_json))
            .WillOnce(Return(R"({"test":0,"continues":false})"_json));
    BasicClient client(std::move(conn));
    auto recv = client.call("test", {}, BasicClient<MockConnection>::CallMode::More);
    EXPECT_EQ(R"({"test":3,"continues":true})"_json, recv());
    EXPECT_EQ(R"({"test":2,"continues":true})"_json, recv());
    EXPECT_EQ(R"({"test":1,"continues":true})"_json, recv());
    EXPECT_EQ(R"({"test":0,"continues":false})"_json, recv());
    EXPECT_EQ(json{}, recv());
}

TEST(Client, Upgrade) {
    auto conn = std::make_unique<MockConnection>();
    EXPECT_CALL(*conn, send(R"({"method":"test","upgrade":true})"_json));
    EXPECT_CALL(*conn, receive()).WillOnce(Return(R"({"test":1})"_json));
    BasicClient client(std::move(conn));
    auto recv = client.call("test", {}, BasicClient<MockConnection>::CallMode::Upgrade);
    EXPECT_EQ(R"({"test":1})"_json, recv());
    EXPECT_EQ(json{}, recv());
}
