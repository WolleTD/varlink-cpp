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
    template<typename SocketT>
    class BasicServer {
    public:
        using ListenConnT = ListeningConnection<SocketT>;
        using ClientConnT = typename ListenConnT::ClientConnection;
    private:
        std::unique_ptr<ListenConnT> serviceConnection;
        std::thread listenThread;
        Service service;

        void acceptLoop() {
            for (;;) {
                try {
                    std::thread{[this](auto conn) {
                        clientLoop(std::move(conn));
                    }, serviceConnection->nextClient()}.detach();
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
                conn.send({{"parameters", msg},
                           {"continues",  true}});
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
        BasicServer(const std::string &address, const std::string &vendor, const std::string &product,
                    const std::string &version, const std::string &url)
                : serviceConnection(std::make_unique<ListenConnT>(address)), listenThread([this]() { acceptLoop(); }),
                  service(vendor, product, version, url) {}

        explicit BasicServer(std::unique_ptr<ListenConnT> listenConn)
                : serviceConnection(std::move(listenConn)), service("","","","") {}

        BasicServer(const BasicServer &src) = delete;
        BasicServer &operator=(const BasicServer &) = delete;
        BasicServer(BasicServer &&src) = delete;
        BasicServer &operator=(BasicServer &&) = delete;

        ~BasicServer() {
            serviceConnection.reset(nullptr);
            listenThread.join();
        }

        template<typename... Args>
        void addInterface(Args&&... args) {
            service.addInterface(std::forward<Args>(args)...);
        }

    };

    template <typename Proto>
    using ThreadedServer = BasicServer<PosixSocket<Proto> >;
}

#endif
