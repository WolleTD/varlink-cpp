/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_TRANSPORT_HPP
#define LIBVARLINK_VARLINK_TRANSPORT_HPP

#include <string>
#include <utility>
#include <varlink/socket.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename SocketT>
class JsonConnection {
   private:
    std::unique_ptr<SocketT> socket;
    using byte_buffer = std::vector<char>;
    byte_buffer readbuf;
    byte_buffer::iterator read_end;

   public:
    // Connect to a service via address
    explicit JsonConnection(const std::string &address) : socket(address), readbuf(BUFSIZ), read_end(readbuf.begin()) {
        socket.connect();
    }

    // Setup message stream on existing connection
    explicit JsonConnection(int posix_fd)
        : socket(std::make_unique<SocketT>(posix_fd)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}
    explicit JsonConnection(std::unique_ptr<SocketT> existingSocket)
        : socket(std::move(existingSocket)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    void send(const json &message) {
        const auto m = message.dump();
        const auto end = m.end() + 1;  // Include \0
        auto sent = socket->write(m.begin(), end);
        while (sent < end) {
            sent = socket->write(sent, end);
        }
    }

    [[nodiscard]] json receive() {
        if (read_end == readbuf.begin()) {
            read_end = socket->read(readbuf.begin(), readbuf.end());
        }
        const auto next_message_end = std::find(readbuf.begin(), read_end, '\0');
        if (next_message_end == read_end) {
            throw std::runtime_error("Incomplete message received: " + std::string(readbuf.begin(), read_end));
        }
        const auto message = std::string(readbuf.begin(), next_message_end);
        read_end = std::copy(next_message_end + 1, read_end, readbuf.begin());
        try {
            return json::parse(message);
        } catch (json::parse_error &e) {
            throw std::runtime_error("Json parse error: " + message);
        }
    }
};

template <typename SocketT>
class ListeningConnection {
   private:
    std::unique_ptr<SocketT> socket;
    std::string socketAddress;

   public:
    using ClientConnection = JsonConnection<SocketT>;

    explicit ListeningConnection(const std::string &address)
        : socket(std::make_unique<SocketT>(address)), socketAddress(address) {
        socket->bind();
        socket->listen(1024);
    }

    explicit ListeningConnection(std::unique_ptr<SocketT> existingSocket)
        : socket(std::move(existingSocket)), socketAddress() {}
    ListeningConnection(const ListeningConnection &src) = delete;
    ListeningConnection &operator=(const ListeningConnection &) = delete;
    ListeningConnection(ListeningConnection &&src) = delete;
    ListeningConnection &operator=(ListeningConnection &&rhs) = delete;

    [[nodiscard]] ClientConnection nextClient() { return ClientConnection(socket->accept(nullptr)); }

    ~ListeningConnection() {
        socket->shutdown(SHUT_RDWR);
        unlink(socketAddress.c_str());
    }
};

}  // namespace varlink

#endif
