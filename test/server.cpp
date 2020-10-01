#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <varlink/server.hpp>

using namespace varlink;
using ::testing::Return;
using ::testing::Throw;

struct FakeServerSocket {
    std::vector<char> fakedata{};
    std::vector<char> writedata{};
    std::vector<char> exp{};

    FakeServerSocket() = default;
    explicit FakeServerSocket([[maybe_unused]] int fd) {}
    ~FakeServerSocket() {
        EXPECT_EQ(writedata, exp);
        EXPECT_TRUE(fakedata.empty());
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        writedata.insert(writedata.end(), begin, end);
        return end;
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
    IteratorT write_fake(IteratorT begin, IteratorT end) {
        fakedata.insert(fakedata.end(), begin, end);
        return end;
    }
    std::string::const_iterator write_fake(const std::string &vec) { return write_fake(vec.cbegin(), vec.cend() + 1); }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        if (fakedata.empty()) {
            throw socket::systemErrorFromErrno("read() failed");
        } else if (static_cast<size_t>(end - begin) > fakedata.size()) {
            auto outEnd = std::copy(fakedata.begin(), fakedata.end(), begin);
            fakedata.clear();
            return outEnd;
        } else {
            std::copy(fakedata.begin(), fakedata.begin() + (end - begin), begin);
            fakedata.erase(fakedata.begin(), fakedata.begin() + (end - begin));
            return end;
        }
    }

    MOCK_METHOD(int, accept, (struct sockaddr_un *));
    MOCK_METHOD(void, shutdown, (int));
};

class ServerHandleTest : public ::testing::Test {
   protected:
    using ClientConnT = typename BasicServer<FakeServerSocket>::ClientConnT;
    BasicServer<FakeServerSocket> server{std::make_unique<FakeServerSocket>(), {}};
    std::unique_ptr<FakeServerSocket> client_sock;
    ClientConnT client;

    template <typename... Args>
    void SetUp(Args &&...args) {
        server.addInterface(
            "interface org.test\nmethod T() -> ()\nmethod P(A: int) -> ()\n"
            "method R() -> (B: int)\nmethod PR(A: int) -> (B: int)\n",
            CallbackMap{
                {"T", [] VarlinkCallback { return json::object(); }},
                {"P", [] VarlinkCallback { return json::object(); }},
                {"R",
                 [] VarlinkCallback {
                     return {{"B", 42}};
                 }},
                {"PR",
                 [] VarlinkCallback {
                     return {{"B", 21}};
                 }},
            });
        client_sock = std::make_unique<FakeServerSocket>();
        (client_sock->write_fake(args), ...);
    }

    template <typename... Args>
    void Expect(Args &&...args) {
        (client_sock->write_exp(args), ...);
        client = ClientConnT{std::move(client_sock)};
    }
};

TEST(ServerTest, CreateAcceptDestroy) {
    auto socket = std::make_unique<FakeServerSocket>();
    EXPECT_CALL(*socket, accept(nullptr)).WillOnce(Return(8));
    EXPECT_CALL(*socket, shutdown(SHUT_RDWR));
    auto server = BasicServer<FakeServerSocket>{std::move(socket), {}};
    EXPECT_NO_THROW(server.accept());
}

TEST_F(ServerHandleTest, Simple) {
    SetUp(R"({"method":"org.test.T"})", R"({"method":"org.test.P","parameters":{"A":10}})",
          R"({"method":"org.test.R"})", R"({"method":"org.test.PR","parameters":{"A":10}})");
    Expect(R"({"parameters":{}})", R"({"parameters":{}})", R"({"parameters":{"B":42}})", R"({"parameters":{"B":21}})");
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));  // 0 Bytes -> terminate successfully
}
