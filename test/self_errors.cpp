#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
namespace fs = std::filesystem;

TEST(VarlinkAPI, UnsupportedProtocol) {
    EXPECT_THROW(varlink_server("udp:127.0.0.1:1234", {}), std::invalid_argument);
    EXPECT_THROW(varlink_client("udp:127.0.0.1:1234"), std::invalid_argument);
    EXPECT_THROW(varlink_server("http:127.0.0.1:1234", {}), std::invalid_argument);
    EXPECT_THROW(varlink_client("http:127.0.0.1:1234"), std::invalid_argument);
    EXPECT_THROW(varlink_server("abc:/dev/null", {}), std::invalid_argument);
    EXPECT_THROW(varlink_client("abc:/dev/zero"), std::invalid_argument);
}

TEST(VarlinkAPI, InvalidPort) {
    EXPECT_THROW(varlink_server("tcp:127.0.0.1:ABC", {}), std::invalid_argument);
    EXPECT_THROW(varlink_client("tcp:127.0.0.1:ABC"), std::invalid_argument);
}

TEST(VarlinkServer, CreateDestroy) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto server = varlink_server("unix:" + address.string(), {});
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(VarlinkServer, Move) {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto server = varlink_server("unix:" + address.string(), {});
        auto server2 = std::move(server);
        EXPECT_TRUE(fs::is_socket(address));
    }
    EXPECT_FALSE(fs::exists(address));
}

TEST(VarlinkServer, AlreadyExists) {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    EXPECT_THROW(varlink_server("unix:" + address.string(), {}), std::system_error);
    EXPECT_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST(VarlinkServer, InvalidAddress) {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    EXPECT_THROW(varlink_server("unix:" + (dir / "test.sock").string(), {}), std::system_error);
    EXPECT_FALSE(fs::exists(dir));
}

TEST(VarlinkServer, NoPermissions) {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    EXPECT_THROW(varlink_server("unix:" + (dir / "test.sock").string(), {}), std::system_error);
    EXPECT_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST(VarlinkServer, FilenameTooLong) {
    const fs::path path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
        "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(varlink_server("unix:" + path.string(), {}), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}

TEST(UnixSocket, ListenFails) {
    const fs::path path = "test-ListenFails.sock";
    auto sock = socket::unix(socket::mode::raw, path.string());
    EXPECT_FALSE(fs::exists(path));
    EXPECT_THROW(sock.listen(1024), std::system_error);
    EXPECT_FALSE(fs::exists(path));
}

GTEST_API_ int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    printf("Running main() from %s\n", __FILE__);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
