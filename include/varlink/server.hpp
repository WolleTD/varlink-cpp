/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <functional>
#include <string>
#include <system_error>
#include <utility>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename SocketT>
class BasicThreadedServer {
   public:
    using ClientConnT = JsonConnection<SocketT>;

   private:
    std::unique_ptr<SocketT> listenSocket;
    std::thread listenThread;
    Service service;

    void acceptLoop() {
        for (;;) {
            try {
                std::thread{[this](auto conn) { clientLoop(std::move(conn)); },
                            ClientConnT(listenSocket->accept(nullptr))}
                    .detach();
            } catch (std::system_error &e) {
                if (e.code() == std::errc::invalid_argument) {
                    // accept() fails with EINVAL when the socket isn't listening, i.e. shutdown
                    break;
                } else {
                    std::cerr << "Error accepting client (" << e.code() << "): " << e.what() << std::endl;
                }
            }
        }
    }

    // Template dependency: ClientConnection
    void clientLoop(ClientConnT conn) {
        const auto sendmore = [&conn](const json &msg) {
            assert(msg.is_object());
            conn.send({{"parameters", msg}, {"continues", true}});
        };

        for (;;) {
            try {
                const Message message{conn.receive()};
                const auto reply = service.messageCall(message, sendmore);
                if (reply.is_object()) {
                    conn.send(reply);
                }
            } catch (std::system_error &e) {
                if (e.code() != std::error_code(0, std::system_category())) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                }
                break;
            } catch (std::exception &e) {
                std::cerr << "Error: " << e.what() << std::endl;
                break;
            }
        }
    }

   public:
    BasicThreadedServer(const std::string &address, const Service::Description &description)
        : listenSocket(std::make_unique<SocketT>(socket::Mode::Listen, address)),
          listenThread([this]() { acceptLoop(); }),
          service(description) {}

    explicit BasicThreadedServer(std::unique_ptr<SocketT> listenConn, const Service::Description &description)
        : listenSocket(std::move(listenConn)), service(description) {}

    BasicThreadedServer(const BasicThreadedServer &src) = delete;
    BasicThreadedServer &operator=(const BasicThreadedServer &) = delete;
    BasicThreadedServer(BasicThreadedServer &&src) = delete;
    BasicThreadedServer &operator=(BasicThreadedServer &&) = delete;

    ~BasicThreadedServer() {
        listenSocket->shutdown(SHUT_RDWR);
        listenThread.join();
    }

    template <typename... Args>
    void addInterface(Args &&...args) {
        service.addInterface(std::forward<Args>(args)...);
    }
};

using ThreadedServer = BasicThreadedServer<socket::UnixSocket>;
}  // namespace varlink

#endif
