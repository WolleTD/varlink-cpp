#include <varlink/client.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace varlink;
using ::testing::Return;

class ConnectionTest : public ::testing::Test {
protected:
    std::stringstream ss{};
    JsonConnection conn{ss.rdbuf(), ss.rdbuf()};
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
    auto conn = JsonConnection((std::stringbuf *)nullptr, (std::stringbuf *)nullptr);
    EXPECT_THROW(conn.send(R"({"object":true})"_json), std::system_error);
}
