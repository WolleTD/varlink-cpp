#include <catch2/catch.hpp>

#include <filesystem>
#include <fstream>
#include <varlink/client.hpp>
#include <varlink/server.hpp>

using namespace varlink;
namespace fs = std::filesystem;

TEST_CASE("VarlinkAPI, UnsupportedProtocol") {
    REQUIRE_THROWS_AS(threaded_server("udp:127.0.0.1:1234", {}), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_client("udp:127.0.0.1:1234"), std::invalid_argument);
    REQUIRE_THROWS_AS(threaded_server("http:127.0.0.1:1234", {}), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_client("http:127.0.0.1:1234"), std::invalid_argument);
    REQUIRE_THROWS_AS(threaded_server("abc:/dev/null", {}), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_client("abc:/dev/zero"), std::invalid_argument);
}

TEST_CASE("VarlinkAPI, InvalidPort") {
    REQUIRE_THROWS_AS(threaded_server("tcp:127.0.0.1:ABC", {}), std::invalid_argument);
    REQUIRE_THROWS_AS(varlink_client("tcp:127.0.0.1:ABC"), std::invalid_argument);
}

TEST_CASE("VarlinkServer, CreateDestroy") {
    const fs::path address = "testCreateDirectory.sock";
    fs::remove(address);
    {
        auto server = threaded_server("unix:" + address.string(), {});
        REQUIRE(fs::is_socket(address));
    }
    REQUIRE_FALSE(fs::exists(address));
}

TEST_CASE("VarlinkServer, AlreadyExists") {
    const fs::path address = "testAlreadyExists.sock";
    std::ofstream{address}.close();
    REQUIRE_THROWS_AS(threaded_server("unix:" + address.string(), {}), std::system_error);
    REQUIRE(fs::exists(address));
    fs::remove(address);
}

TEST_CASE("VarlinkServer, InvalidAddress") {
    const fs::path dir = "nonexistent-directory-InvalidAddress";
    fs::remove(dir);
    REQUIRE_THROWS_AS(threaded_server("unix:" + (dir / "test.sock").string(), {}), std::system_error);
    REQUIRE_FALSE(fs::exists(dir));
}

TEST_CASE("VarlinkServer, NoPermissions") {
    const fs::path dir = "testdir-NoPermissions";
    fs::create_directory(dir);
    chmod(dir.c_str(), 0500);
    REQUIRE_THROWS_AS(threaded_server("unix:" + (dir / "test.sock").string(), {}), std::system_error);
    REQUIRE_FALSE(fs::exists(dir / "test.sock"));
    fs::remove(dir);
}

TEST_CASE("VarlinkServer, FilenameTooLong") {
    const fs::path path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un.saddr."
        "requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    REQUIRE_THROWS_AS(threaded_server("unix:" + path.string(), {}), std::system_error);
    REQUIRE_FALSE(fs::exists(path));
}
