#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <varlink/client.hpp>

using namespace varlink;
using ::testing::Return;
using ::testing::Throw;

struct FakeSocket {
    size_t write_max{BUFSIZ};
    std::vector<char> data{};
    std::vector<char> exp{};

    FakeSocket() = default;
    explicit FakeSocket([[maybe_unused]] int fd) {}
    ~FakeSocket() { EXPECT_EQ(data, exp); }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        if (static_cast<size_t>(end - begin) > write_max) {
            data.insert(data.end(), begin, begin + static_cast<long>(write_max));
            return begin + static_cast<long>(write_max);
        } else {
            data.insert(data.end(), begin, end);
            return end;
        }
    }
    std::string::const_iterator write(const std::string &vec) { return write(vec.cbegin(), vec.cend() + 1); }
    template <typename T>
    typename T::const_iterator write(const T &vec) {
        return write(vec.cbegin(), vec.cend());
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write_exp(IteratorT begin, IteratorT end) {
        exp.insert(exp.end(), begin, end);
        return end;
    }
    std::string::const_iterator write_exp(const std::string &vec) { return write_exp(vec.cbegin(), vec.cend() + 1); }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        if (data.empty()) {
            throw socket::system_error_from_errno("read() failed");
        } else if (static_cast<size_t>(end - begin) > data.size()) {
            auto outEnd = std::copy(data.begin(), data.end(), begin);
            data.clear();
            return outEnd;
        } else {
            std::copy(data.begin(), data.begin() + (end - begin), begin);
            data.erase(data.begin(), data.begin() + (end - begin));
            return end;
        }
    }

    MOCK_METHOD(int, accept, (struct sockaddr_un * addr));
    MOCK_METHOD(void, shutdown, (int how));
};

class ConnectionRead : public ::testing::Test {
   protected:
    using test_connection = basic_json_connection<socket::type::unspec, FakeSocket>;
    std::unique_ptr<FakeSocket> socket{};
    test_connection conn{-1};

    void SetUp() override { socket = std::make_unique<FakeSocket>(); }

    template <typename... Args>
    void SetUp(Args &&...args) {
        socket = std::make_unique<FakeSocket>();
        (socket->write(args), ...);
        conn = test_connection{std::move(socket)};
    }
};

TEST_F(ConnectionRead, Success) {
    SetUp(R"({"object":true})", R"("string")", R"({"int":42})", R"({"float":3.14})", R"(["array"])", R"(null)");
    EXPECT_EQ(conn.receive()["object"].get<bool>(), true);
    EXPECT_EQ(conn.receive().get<std::string>(), "string");
    EXPECT_EQ(conn.receive()["int"].get<int>(), 42);
    EXPECT_DOUBLE_EQ(conn.receive()["float"].get<double>(), 3.14);
    EXPECT_EQ(conn.receive()[0].get<std::string>(), "array");
    EXPECT_EQ(conn.receive().is_null(), true);
}

TEST_F(ConnectionRead, ThrowPartial) {
    SetUp(R"({"object":)");
    EXPECT_THROW((void)conn.receive(), std::invalid_argument);
}

TEST_F(ConnectionRead, ThrowTrailing) {
    SetUp(R"({"object":true}trailing)");
    EXPECT_THROW((void)conn.receive(), std::invalid_argument);
}

TEST_F(ConnectionRead, ThrowIncomplete) {
    // TODO: This behaviour is invalid if we ever expect partial transmissions
    SetUp(std::vector<char>{'{', '"', 'c', 'd', 'e'});
    EXPECT_THROW((void)conn.receive(), std::invalid_argument);
}

TEST_F(ConnectionRead, ThrowEOF) {
    conn = test_connection{std::move(socket)};
    EXPECT_THROW((void)conn.receive(), std::system_error);
}

TEST_F(ConnectionRead, SuccesThenThrowEOF) {
    SetUp(R"({"object":true})");
    EXPECT_EQ(conn.receive()["object"].get<bool>(), true);
    EXPECT_THROW((void)conn.receive(), std::system_error);
}

class ConnectionWrite : public ::testing::Test {
   protected:
    using test_connection = basic_json_connection<socket::type::unspec, FakeSocket>;
    std::unique_ptr<FakeSocket> socket{};
    test_connection conn{-1};

    void SetUp() override { socket = std::make_unique<FakeSocket>(); }

    template <typename... Args>
    void SetUp(size_t write_max, Args &&...args) {
        socket = std::make_unique<FakeSocket>();
        socket->write_max = write_max;
        (socket->write_exp(args), ...);
        conn = test_connection{std::move(socket)};
    }
};

TEST_F(ConnectionWrite, Success) {
    SetUp(BUFSIZ, "{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
    conn.send(R"({"object":true})"_json);
    conn.send(R"("string")"_json);
    conn.send(R"({"float":3.14})"_json);
    conn.send(R"(null)"_json);
}

TEST_F(ConnectionWrite, Partial) {
    SetUp(10, R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
    conn.send(R"({"s":1})"_json);
    conn.send(R"({"object":true,"toolong":true})"_json);
    conn.send(R"({"last":true})"_json);
}
