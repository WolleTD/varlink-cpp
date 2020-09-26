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

class ConnectionTest : public ::testing::Test {
protected:
    std::stringstream ss{};
    StreamingConnection conn{ss.rdbuf(), ss.rdbuf()};
};

TEST_F(ConnectionTest, Read) {
    ss << R"({"object":true})" << '\0'
            << R"("string")" << '\0'
            << R"({"int":42})" << '\0'
            << R"({"float":3.14})" << '\0'
            << R"(["array"])" << '\0'
            << R"(null)" << '\0';
    EXPECT_EQ(conn.receive()["object"].get<bool>(), true);
    EXPECT_EQ(conn.receive().get<std::string>(), "string");
    EXPECT_EQ(conn.receive()["int"].get<int>(), 42);
    EXPECT_DOUBLE_EQ(conn.receive()["float"].get<double>(), 3.14);
    EXPECT_EQ(conn.receive()[0].get<std::string>(), "array");
    EXPECT_EQ(conn.receive().is_null(), true);
    EXPECT_THROW((void)conn.receive(), std::system_error);
}

TEST_F(ConnectionTest, ReadThrowPartial) {
    ss << R"({"object":)" << '\0';
    EXPECT_THROW((void)conn.receive(), std::invalid_argument);
}

TEST_F(ConnectionTest, ReadThrowTrailing) {
    ss << R"({"object":true}trailing)" << '\0';
    EXPECT_THROW((void)conn.receive(), std::invalid_argument);
}

TEST_F(ConnectionTest, ReadThrowEOF) {
    ss << R"({"object":tr)";
    EXPECT_THROW((void)conn.receive(), std::system_error);
}

TEST_F(ConnectionTest, Write) {
    conn.send(R"({"object":true})"_json);
    conn.send(R"("string")"_json);
    conn.send(R"({"float":3.14})"_json);
    conn.send(R"(null)"_json);
    std::stringstream exp;
    exp << "{\"object\":true}" << '\0' << "\"string\"" << '\0' << "{\"float\":3.14}" << '\0' << "null" << '\0';
    EXPECT_EQ(ss.str(), exp.str());
}

TEST_F(ConnectionTest, WriteThrowEOF) {
    auto conn = StreamingConnection((std::stringbuf*)nullptr, (std::stringbuf*)nullptr);
    EXPECT_THROW(conn.send(R"({"object":true})"_json), std::system_error);
}