#include <varlink.hpp>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace varlink;
using ::testing::_;
using ::testing::Return;

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

TEST(Service, Create) {
    auto comm = std::make_unique<MockServiceConnection>();
    EXPECT_CALL(*comm, listen(_));
    auto svc = TestService(std::move(comm));
}