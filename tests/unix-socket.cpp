#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <varlink/transport.hpp>

using namespace varlink::socket;
namespace fs = std::filesystem;

TEST_CASE("UnixSocket", "CreateDestroy") {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto sock = UnixSocket(Mode::Listen, address.string());
        REQUIRE_TRUE(fs::is_socket(address));
    }
    REQUIRE_FALSE(fs::exists(address));
}

TEST_CASE("UnixSocket", "AlreadyExists") {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    REQUIRE_THROWS_AS(UnixSocket(Mode::Listen, address.string()), std::system_error);
    REQUIRE_TRUE(fs::exists(address));
    fs::remove(address);
}

TEST_CASE("UnixSocket", "InvalidAddress") {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    REQUIRE_THROWS_AS(UnixSocket(Mode::Listen, (dir / "test.sock").string()), std::system_error);
    REQUIRE_FALSE(fs::exists(dir));
}

TEST_CASE("UnixSocket", "NoPermissions") {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    REQUIRE_THROWS_AS(UnixSocket(Mode::Listen, (dir / "test.sock").string()), std::system_error);
    REQUIRE_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST_CASE("UnixSocket", "FilenameTooLong") {
    const fs::path path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
        "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    REQUIRE_THROWS_AS(UnixSocket(Mode::Listen, path.string()), std::system_error);
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE("UnixSocket", "ListenFails") {
    const fs::path path = "test-ListenFails.sock";
    auto sock = UnixSocket(Mode::Raw, path.string());
    REQUIRE_FALSE(fs::exists(path));
    REQUIRE_THROWS_AS(sock.listen(1024), std::system_error);
    REQUIRE_FALSE(fs::exists(path));
}
