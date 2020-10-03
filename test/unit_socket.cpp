#include <gtest/gtest.h>

#include <varlink/socket.hpp>

using namespace varlink::socket;

TEST(SocketAddress, Udp) {
    auto addr = type::Unix("/tmp/test.sock");
    EXPECT_EQ(addr.sun_family, AF_UNIX);
    EXPECT_STREQ(addr.sun_path, "/tmp/test.sock");
}

TEST(SocketAddress, UdpTooLong) {
    const std::string_view path =
        "some.very.long.filename.that.does.not.fit.into.sockaddr_un."
        "saddr.requires.two.lines.in.c++.to.be.readable.as.it.has.to.be.longer.than.108.characters";
    EXPECT_THROW(type::Unix{path}, std::system_error);
}

TEST(SocketAddress, Tcp) {
    auto addr = type::TCP("127.0.0.1", 123);
    EXPECT_EQ(addr.sin_family, AF_INET);
    EXPECT_EQ(addr.sin_port, htons(123));
    EXPECT_EQ(addr.sin_addr.s_addr, htonl(0x7F000001));
}

TEST(SocketAddress, TcpInvalid) {
    EXPECT_THROW(type::TCP("1", 123), std::invalid_argument);
    EXPECT_THROW(type::TCP("/tmp/test.sock", 123), std::invalid_argument);
    EXPECT_THROW(type::TCP("www.google.de", 123), std::invalid_argument);
    EXPECT_THROW(type::TCP("localhost", 123), std::invalid_argument);
}

TEST(PosixSocket, Move) {
    auto sock1 = UnixSocket(8);
    auto sock2 = std::move(sock1);
    EXPECT_EQ(sock1.remove_fd(), -1);  // NOLINT: test move
    EXPECT_EQ(sock2.remove_fd(), 8);
    EXPECT_EQ(sock2.remove_fd(), -1);
    sock1 = UnixSocket(7);
    auto sock3(std::move(sock1));
    EXPECT_EQ(sock1.remove_fd(), -1);  // NOLINT: test move
    EXPECT_EQ(sock3.remove_fd(), 7);
}