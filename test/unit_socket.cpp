#include <gtest/gtest.h>

#include <varlink/socket.hpp>

using namespace varlink::socket;

TEST(SocketAddress, Udp) {
    auto addr = type::unix("/tmp/test.sock");
    EXPECT_EQ(addr.sun_family, AF_UNIX);
    EXPECT_STREQ(addr.sun_path, "/tmp/test.sock");
}

TEST(SocketAddress, UdpTooLong) {
    const std::string_view path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un."
        "saddr.requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(type::unix{path}, std::system_error);
}

TEST(SocketAddress, Tcp) {
    auto addr = type::tcp("127.0.0.1", 123);
    EXPECT_EQ(addr.sin_family, AF_INET);
    EXPECT_EQ(addr.sin_port, htons(123));
    EXPECT_EQ(addr.sin_addr.s_addr, htonl(0x7F000001));
}

TEST(SocketAddress, TcpInvalid) {
    EXPECT_THROW(type::tcp("1", 123), std::invalid_argument);
    EXPECT_THROW(type::tcp("/tmp/test.sock", 123), std::invalid_argument);
    EXPECT_THROW(type::tcp("www.google.de", 123), std::invalid_argument);
    EXPECT_THROW(type::tcp("localhost", 123), std::invalid_argument);
}

TEST(PosixSocket, Move) {
    auto sock1 = unix(8);
    auto sock2 = std::move(sock1);
    EXPECT_EQ(sock1.remove_fd(), -1);  // NOLINT: test move
    EXPECT_EQ(sock2.remove_fd(), 8);
    EXPECT_EQ(sock2.remove_fd(), -1);
    sock1 = unix(7);
    auto sock3(std::move(sock1));
    EXPECT_EQ(sock1.remove_fd(), -1);  // NOLINT: test move
    EXPECT_EQ(sock3.remove_fd(), 7);
    EXPECT_EQ(remove_fd(std::move(sock3)), -1);
    auto sock4 = unix(mode::raw, "test-move.socket");
    auto sock5 = std::move(sock4);
    EXPECT_EQ(sock4.remove_fd(), -1);  // NOLINT: test move
    EXPECT_STREQ(sock4.get_sockaddr().sun_path, "");  // NOLINT: test move
    EXPECT_STREQ(sock5.get_sockaddr().sun_path, "test-move.socket");
}
