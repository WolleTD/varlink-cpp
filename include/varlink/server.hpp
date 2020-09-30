/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <functional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <varlink/transport.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
template <typename SocketT>
class BasicServer {
   public:
    using ClientConnT = JsonConnection<SocketT>;

   protected:
    std::unique_ptr<SocketT> listenSocket;
    Service service;

   public:
    template <typename... Args>
    explicit BasicServer(const Service::Description &description, Args &&...args)
        : listenSocket(std::make_unique<SocketT>(socket::Mode::Listen, std::forward<Args>(args)...)),
          service(description) {}

    explicit BasicServer(std::unique_ptr<SocketT> listenConn, const Service::Description &description)
        : listenSocket(std::move(listenConn)), service(description) {}

    BasicServer(const BasicServer &src) = delete;
    BasicServer &operator=(const BasicServer &) = delete;
    BasicServer(BasicServer &&src) noexcept = default;
    BasicServer &operator=(BasicServer &&) noexcept = default;

    ~BasicServer() { listenSocket->shutdown(SHUT_RDWR); }

    ClientConnT accept() { return ClientConnT(listenSocket->accept(nullptr)); }

    void processConnection(ClientConnT &conn) {
        const auto sendmore = [&conn](const json &msg) {
            assert(msg.is_object());
            conn.send({{"parameters", msg}, {"continues", true}});
        };

        try {
            const Message message{conn.receive()};
            const auto reply = service.messageCall(message, sendmore);
            if (reply.is_object()) {
                conn.send(reply);
            }
        } catch (std::system_error &e) {
            if (e.code() != std::error_code(0, std::system_category())) {
                throw;
            }
        }
    }

    template <typename... Args>
    void addInterface(Args &&...args) {
        service.addInterface(std::forward<Args>(args)...);
    }
};

template <typename SocketT>
class ThreadedServer : BasicServer<SocketT> {
   public:
    using Base = BasicServer<SocketT>;
    using typename Base::ClientConnT;

   private:
    std::thread listenThread;

    void acceptLoop() {
        while (true) {
            try {
                // TODO: don't detach, thread pool
                std::thread{[this](auto conn) { clientLoop(std::move(conn)); }, Base::accept()}.detach();
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
        try {
            while (true) Base::processConnection(conn);
        } catch (std::exception &e) {
            std::cerr << "Terminate connection: " << e.what() << std::endl;
        }
    }

   public:
    template <typename... Args>
    explicit ThreadedServer(const Service::Description &description, Args &&...args)
        : BasicServer<SocketT>(description, std::forward<Args>(args)...), listenThread([this]() { acceptLoop(); }) {}

    explicit ThreadedServer(std::unique_ptr<SocketT> listenConn, const Service::Description &description)
        : BasicServer<SocketT>(std::move(listenConn), description) {}

    ThreadedServer(const ThreadedServer &src) = delete;
    ThreadedServer &operator=(const ThreadedServer &) = delete;
    ThreadedServer(ThreadedServer &&src) noexcept = default;
    ThreadedServer &operator=(ThreadedServer &&src) noexcept = default;

    ~ThreadedServer() {
        // There is no dedicated thread communication yet, so we use the socket to terminate
        // the listening thread TODO: thread pool will fix this
        Base::listenSocket->shutdown(SHUT_RDWR);
        listenThread.join();
    }

    void join() { listenThread.join(); }

    using Base::addInterface;
};

using ThreadedUnixServer = ThreadedServer<socket::UnixSocket>;
using ThreadedTCPServer = ThreadedServer<socket::TCPSocket>;
}  // namespace varlink

#endif
