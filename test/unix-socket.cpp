#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <varlink/transport.hpp>

using namespace varlink::socket;
namespace fs = std::filesystem;

TEST(UnixSocket, CreateDestroy) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto sock = UnixSocket(Mode::Listen, address.string());
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(UnixSocket, AlreadyExists) {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    EXPECT_THROW(UnixSocket(Mode::Listen, address.string()), std::system_error);
    EXPECT_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST(UnixSocket, InvalidAddress) {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    EXPECT_THROW(UnixSocket(Mode::Listen, (dir / "test.sock").string()), std::system_error);
    EXPECT_FALSE(fs::exists(dir));
}

TEST(UnixSocket, NoPermissions) {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    EXPECT_THROW(UnixSocket(Mode::Listen, (dir / "test.sock").string()), std::system_error);
    EXPECT_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST(UnixSocket, FilenameTooLong) {
    const fs::path path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
        "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(UnixSocket(Mode::Listen, path.string()), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}

TEST(UnixSocket, ListenFails) {
    const fs::path path = "test-ListenFails.sock";
    auto sock = UnixSocket(Mode::Raw, path.string());
    EXPECT_FALSE(fs::exists(path));
    EXPECT_THROW(sock.listen(1024), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}
