#include <fstream>
#include <filesystem>
#include <varlink/transport.hpp>
#include <gtest/gtest.h>

using namespace varlink;
namespace fs = std::filesystem;

using ListenSock = ListeningConnection<PosixSocket<Unix> >;

TEST(ListenSocket, CreateDestroy) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto sock = ListenSock(address);
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(ListenSocket, AlreadyExists) {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    EXPECT_THROW(ListenSock{address}, std::system_error);
    EXPECT_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST(ListenSocket, InvalidAddress) {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    EXPECT_THROW(ListenSock{dir / "test.sock"}, std::system_error);
    EXPECT_FALSE(fs::exists(dir));
}

TEST(ListenSocket, NoPermissions) {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    EXPECT_THROW(ListenSock{dir / "test.sock"}, std::system_error);
    EXPECT_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST(ListenSocket, FilenameTooLong) {
    const fs::path path = "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
            "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(ListenSock{path}, std::system_error);
    EXPECT_FALSE(fs::exists(path));
}
