#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <varlink/transport.hpp>

using namespace varlink;
namespace fs = std::filesystem;

using ListenSock = socket::UnixSocket;

TEST(ListenSocket, CreateDestroy) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto sock = ListenSock(socket::Mode::Listen, address.string());
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(ListenSocket, AlreadyExists) {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    EXPECT_THROW(ListenSock(socket::Mode::Listen, address.string()), std::system_error);
    EXPECT_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST(ListenSocket, InvalidAddress) {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    EXPECT_THROW(ListenSock(socket::Mode::Listen, (dir / "test.sock").string()), std::system_error);
    EXPECT_FALSE(fs::exists(dir));
}

TEST(ListenSocket, NoPermissions) {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    EXPECT_THROW(ListenSock(socket::Mode::Listen, (dir / "test.sock").string()), std::system_error);
    EXPECT_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST(ListenSocket, FilenameTooLong) {
    const fs::path path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
        "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(ListenSock(socket::Mode::Listen, path.string()), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}

TEST(ListenSocket, ListenFails) {
    const fs::path path = "test-ListenFails.sock";
    auto sock = ListenSock(socket::Mode::Raw, path.string());
    EXPECT_FALSE(fs::exists(path));
    EXPECT_THROW(sock.listen(1024), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}
