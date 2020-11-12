#include <catch2/catch.hpp>
#include <varlink/client.hpp>

using namespace varlink;

inline std::system_error system_error_from_errno(const std::string& what)
{
    if (errno == 0) {
        return {std::make_error_code(std::errc{}), what};
    }
    else {
        return {std::error_code(errno, std::system_category()), what};
    }
}

struct FakeSocket : public net::socket_base {
    using protocol_type = net::local::stream_protocol;
    using executor_type = net::any_io_executor;
    size_t write_max{BUFSIZ};
    std::vector<char> data{};
    std::vector<char> exp{};
    net::io_context* ctx_;

    explicit FakeSocket(net::io_context& ctx) : ctx_(&ctx) {}
    FakeSocket(
        [[maybe_unused]] net::io_context& ctx,
        [[maybe_unused]] net::local::stream_protocol p,
        [[maybe_unused]] int fd)
        : ctx_(&ctx)
    {
    }
    FakeSocket(FakeSocket&& r) noexcept = default;
    FakeSocket& operator=(FakeSocket&& r) noexcept = default;
    ~FakeSocket() { REQUIRE(data == exp); }

    executor_type get_executor() { return ctx_->get_executor(); }

    template <
        typename IteratorT,
        typename = std::enable_if_t<
            std::is_convertible_v<typename IteratorT::value_type, char>>>
    IteratorT write(IteratorT begin, IteratorT end)
    {
        if (static_cast<size_t>(end - begin) > write_max) {
            data.insert(data.end(), begin, begin + static_cast<long>(write_max));
            return begin + static_cast<long>(write_max);
        }
        else {
            data.insert(data.end(), begin, end);
            return end;
        }
    }
    std::string::const_iterator write(const std::string& vec)
    {
        return write(vec.cbegin(), vec.cend() + 1);
    }
    template <typename T>
    typename T::const_iterator write(const T& vec)
    {
        return write(vec.cbegin(), vec.cend());
    }

    size_t send(const net::const_buffer& buffer)
    {
        for (size_t i = 0; i < std::min(write_max, buffer.size()); i++) {
            data.push_back(static_cast<const char*>(buffer.data())[i]);
        }
        return std::min(write_max, buffer.size());
    }

    template <
        typename IteratorT,
        typename = std::enable_if_t<
            std::is_convertible_v<typename IteratorT::value_type, char>>>
    IteratorT write_exp(IteratorT begin, IteratorT end)
    {
        exp.insert(exp.end(), begin, end);
        return end;
    }
    std::string::const_iterator write_exp(const std::string& vec)
    {
        return write_exp(vec.cbegin(), vec.cend() + 1);
    }

    size_t receive(const net::mutable_buffer& buffer)
    {
        const auto size = data.size();
        if (data.empty()) {
            throw system_error_from_errno("read() failed");
        }
        else if (buffer.size() > size) {
            std::memcpy(buffer.data(), data.data(), size);
            data.clear();
            return size;
        }
        else {
            std::memcpy(buffer.data(), data.data(), buffer.size());
            data.erase(
                data.begin(),
                data.begin() + static_cast<ptrdiff_t>(buffer.size()));
            return buffer.size();
        }
    }
};

class ConnectionRead {
  protected:
    using test_connection = json_connection<FakeSocket>;
    net::io_context ctx{};
    FakeSocket socket;
    test_connection conn;

    ConnectionRead() : socket(ctx), conn(FakeSocket(ctx)) {}

    template <typename... Args>
    void SetUp(Args&&... args)
    {
        (socket.write(args), ...);
        conn = test_connection{std::move(socket)};
    }
};

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, Success")
{
    SetUp(
        R"({"object":true})",
        R"("string")",
        R"({"int":42})",
        R"({"float":3.14})",
        R"(["array"])",
        R"(null)");
    REQUIRE(conn.receive()["object"].get<bool>() == true);
    REQUIRE(conn.receive().get<std::string>() == "string");
    REQUIRE(conn.receive()["int"].get<int>() == 42);
    REQUIRE(conn.receive()["float"].get<double>() == Approx(3.14));
    REQUIRE(conn.receive()[0].get<std::string>() == "array");
    REQUIRE(conn.receive().is_null() == true);
}

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, ThrowPartial")
{
    SetUp(R"({"object":)");
    REQUIRE_THROWS_AS((void)conn.receive(), std::invalid_argument);
}

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, ThrowTrailing")
{
    SetUp(R"({"object":true}trailing)");
    REQUIRE_THROWS_AS((void)conn.receive(), std::invalid_argument);
}

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, ThrowIncomplete")
{
    // TODO: This behaviour is invalid if we ever expect partial transmissions
    SetUp(std::vector<char>{'{', '"', 'c', 'd', 'e'});
    REQUIRE_THROWS_AS((void)conn.receive(), std::invalid_argument);
}

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, ThrowEOF")
{
    conn = test_connection{std::move(socket)};
    REQUIRE_THROWS_AS((void)conn.receive(), std::system_error);
}

TEST_CASE_METHOD(ConnectionRead, "ConnectionRead, SuccesThenThrowEOF")
{
    SetUp(R"({"object":true})");
    REQUIRE(conn.receive()["object"].get<bool>() == true);
    REQUIRE_THROWS_AS((void)conn.receive(), std::system_error);
}

class ConnectionWrite {
  protected:
    using test_connection = json_connection<FakeSocket>;
    net::io_context ctx{};
    FakeSocket socket;
    test_connection conn;

    ConnectionWrite() : socket(ctx), conn(FakeSocket(ctx)) {}

    template <typename... Args>
    void SetUp(size_t write_max, Args&&... args)
    {
        socket.write_max = write_max;
        (socket.write_exp(args), ...);
        conn = test_connection{std::move(socket)};
    }
};

TEST_CASE_METHOD(ConnectionWrite, "ConnectionWrite, Success")
{
    SetUp(BUFSIZ, "{\"object\":true}", "\"string\"", "{\"float\":3.14}", "null");
    conn.send(R"({"object":true})"_json);
    conn.send(R"("string")"_json);
    conn.send(R"({"float":3.14})"_json);
    conn.send(R"(null)"_json);
}

TEST_CASE_METHOD(ConnectionWrite, "ConnectionWrite, Partial")
{
    SetUp(
        10, R"({"s":1})", R"({"object":true,"toolong":true})", R"({"last":true})");
    conn.send(R"({"s":1})"_json);
    conn.send(R"({"object":true,"toolong":true})"_json);
    conn.send(R"({"last":true})"_json);
}
