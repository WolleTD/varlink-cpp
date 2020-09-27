#include <fstream>
#include <filesystem>
#include <varlink/transport.hpp>
#include <gtest/gtest.h>

using namespace varlink;
namespace fs = std::filesystem;

TEST(ListenSocket, CreateDestroy) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto sock = ListeningSocket(address, [](){});
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(ListenSocket, AlreadyExists) {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    EXPECT_THROW(ListeningSocket(address, [](){}), std::system_error);
    EXPECT_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST(ListenSocket, InvalidAddress) {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    EXPECT_THROW(ListeningSocket(dir / "test.sock", [](){}), std::system_error);
}

TEST(ListenSocket, NoPermissions) {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0000);
    EXPECT_THROW(ListeningSocket(dir / "test.sock", [](){}), std::system_error);
    fs::remove(dir);
}

TEST(ListenSocket, NoThread) {
    EXPECT_THROW(ListeningSocket("testNoThread.sock", nullptr), std::invalid_argument);
}