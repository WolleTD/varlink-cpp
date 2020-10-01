#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <varlink/server.hpp>

using namespace varlink;
using std::string;
using ::testing::_;
using ::testing::Return;
using ::testing::Throw;

struct FakeServerSocket {
    std::vector<string> fakedata{};
    std::vector<string> writedata{};
    std::vector<string> exp{};

    FakeServerSocket() = default;
    explicit FakeServerSocket([[maybe_unused]] int fd) {}
    ~FakeServerSocket() {
        EXPECT_EQ(writedata, exp);
        EXPECT_TRUE(fakedata.empty());
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT write(IteratorT begin, IteratorT end) {
        writedata.push_back(string(begin, end - 1));
        return end;
    }

    template <typename IteratorT,
              typename = std::enable_if_t<std::is_convertible_v<typename IteratorT::value_type, char> > >
    IteratorT read(IteratorT begin, IteratorT end) {
        if (fakedata.empty()) {
            throw socket::systemErrorFromErrno("read() failed");
        } else {
            if (static_cast<size_t>(end - begin) > fakedata.front().size()) {
                auto outEnd = std::copy(fakedata.front().begin(), fakedata.front().end() + 1, begin);
                fakedata.erase(fakedata.begin());
                return outEnd;
            } else {
                throw socket::systemErrorFromErrno("read() failed");
            }
        }
    }

    MOCK_METHOD(int, accept, (struct sockaddr_un*));
    MOCK_METHOD(void, shutdown, (int));
};

struct MockService {
    struct Description;
    MOCK_METHOD(json, messageCall, (const Message& message, const SendMore& moreCallback));
    MOCK_METHOD(void, setInterface, (Interface&&));
};

using TestSet = std::pair<std::string, std::string>;

class ServerHandleTest : public ::testing::Test {
   protected:
    using TestServerT = BasicServer<FakeServerSocket, MockService>;
    using ClientConnT = typename TestServerT::ClientConnT;
    TestServerT server{std::make_unique<FakeServerSocket>(), std::make_unique<MockService>()};
    std::unique_ptr<FakeServerSocket> client_sock;
    std::unique_ptr<MockService> service;
    ClientConnT client;

    template <typename... Args>
    void SetUp(Args&&... args) {
        auto parseSet = [&](const TestSet& set) {
            client_sock->fakedata.push_back(set.first);
            auto rep = json::parse(set.second);
            if (!rep.is_null()) client_sock->exp.push_back(set.second);
            auto m = Message(json::parse(set.first));
            EXPECT_CALL(*service, messageCall(m, _)).WillOnce(Return(rep));
        };
        client_sock = std::make_unique<FakeServerSocket>();
        service = std::make_unique<MockService>();
        (parseSet(args), ...);
        client = ClientConnT(std::move(client_sock));
        server = TestServerT(std::make_unique<FakeServerSocket>(), std::move(service));
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
    SetUp(TestSet{R"({"method":"org.test.T"})", R"({"parameters":{}})"},
          TestSet{R"({"method":"org.test.P","parameters":{"A":10}})", R"({"parameters":{}})"},
          TestSet{R"({"method":"org.test.R"})", R"({"parameters":{"B":42}})"},
          TestSet{R"({"method":"org.test.PR","parameters":{"A":10}})", R"({"parameters":{"B":21}})"});
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));
    EXPECT_NO_THROW(server.processConnection(client));  // 0 Bytes -> terminate successfully
}

TEST_F(ServerHandleTest, Terminate) { EXPECT_NO_THROW(server.processConnection(client)); }

TEST_F(ServerHandleTest, SystemError) {
    errno = EPIPE;
    EXPECT_THROW(server.processConnection(client), std::system_error);
    errno = 0;
}

TEST_F(ServerHandleTest, DropNullptr) {
    SetUp(TestSet(R"({"method":"org.test.T"})", "null"));
    EXPECT_NO_THROW(server.processConnection(client));
}

TEST_F(ServerHandleTest, TestMore) {
    client_sock = std::make_unique<FakeServerSocket>();
    service = std::make_unique<MockService>();
    auto call = R"({"method":"org.test.T"})";
    client_sock->fakedata.emplace_back(R"({"method":"org.test.T"})");
    client_sock->exp.emplace_back(R"({"continues":true,"parameters":{"a":1}})");
    client_sock->exp.emplace_back(R"({"parameters":{"a":2}})");
    auto m = Message(json::parse(call));
    ON_CALL(*service, messageCall).WillByDefault([](const Message&, const SendMore& more) {
        more(json::parse(R"({"a":1})"));
        return json::parse(R"({"parameters":{"a":2}})");
    });
    EXPECT_CALL(*service, messageCall(m, _));
    client = ClientConnT(std::move(client_sock));
    server = TestServerT(std::make_unique<FakeServerSocket>(), std::move(service));
    EXPECT_NO_THROW(server.processConnection(client));
}

TEST_F(ServerHandleTest, SetInterface) {
    service = std::make_unique<MockService>();
    EXPECT_CALL(*service, setInterface(_));
    server = TestServerT(std::make_unique<FakeServerSocket>(), std::move(service));
    server.setInterface(Interface("interface org.test\nmethod T() -> ()\n"));
}