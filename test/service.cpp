#include <varlink/varlink.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace varlink;
using ::testing::_;
using ::testing::Return;
using ::testing::Throw;

class MockConnection {
public:
    MockConnection([[maybe_unused]] int fd) {}
    MOCK_METHOD(void, send, (const json& message));
    MOCK_METHOD(json, receive, ());
};

class MockServiceConnection {
public:
    MOCK_METHOD(int, nextClientFd, ());
    MOCK_METHOD(void, listen, (const std::function<void()>&));
};

using TestService = BasicService<MockServiceConnection, MockConnection, Interface>;

TEST(Service, CreateListenClose) {
    auto comm = std::make_unique<MockServiceConnection>();
    ON_CALL(*comm, listen(_)).WillByDefault([](const std::function<void()>& f){f();});
    EXPECT_CALL(*comm, listen(_));
    EXPECT_CALL(*comm, nextClientFd()).WillOnce(
            Throw(std::system_error(std::make_error_code(std::errc::invalid_argument))));
    auto svc = TestService(std::move(comm));
}

TEST(Service, SplitFQMethod) {
    std::vector<std::pair<std::string, std::pair<std::string, std::string> > > testdata {
            {"", {"", ""}},
            {"test", {"test", "test"}},
            {"a.b", {"a", "b"}},
            {"a.b.c", {"a.b", "c"}},
            {"a.a.a.a.a.a.a", {"a.a.a.a.a.a", "a"}},
    };
    for (auto& test : testdata) {
        EXPECT_EQ(test.second, splitFqMethod(test.first));
    }
}