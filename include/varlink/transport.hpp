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
    template <typename... Args>
    explicit JsonConnection(Args &&...args)
        : socket(std::make_unique<SocketT>(args...)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    // Setup message stream on existing connection
    explicit JsonConnection(int posix_fd)
        : socket(std::make_unique<SocketT>(posix_fd)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    explicit JsonConnection(std::unique_ptr<SocketT> existingSocket)
        : socket(std::move(existingSocket)), readbuf(BUFSIZ), read_end(readbuf.begin()) {}

    JsonConnection(const JsonConnection&) = delete;
    JsonConnection& operator=(const JsonConnection&) = delete;
    JsonConnection(JsonConnection&&) noexcept = default;
    JsonConnection& operator=(JsonConnection&&) noexcept = default;

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

}  // namespace varlink

#endif
