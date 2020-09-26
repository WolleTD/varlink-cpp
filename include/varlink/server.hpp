/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_SERVER_HPP
#define LIBVARLINK_VARLINK_SERVER_HPP

#include <cerrno>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <varlink/client.hpp>
#include <varlink/varlink.hpp>

namespace varlink {
    class ListeningSocket {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        int listen_fd{-1};
    public:

        explicit ListeningSocket(std::string address, const std::function<void()> &listener = nullptr)
                : socketAddress(std::move(address)) {
            listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (listen_fd < 0) {
                throw systemErrorFromErrno("socket() failed");
            }
            struct sockaddr_un addr{AF_UNIX, ""};
            if (socketAddress.length() + 1 > sizeof(addr.sun_path)) {
                throw std::system_error{std::make_error_code(std::errc::filename_too_long)};
            }
            socketAddress.copy(addr.sun_path, socketAddress.length());
            if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                throw systemErrorFromErrno("bind() failed");
            }
            if (::listen(listen_fd, 1024) < 0) {
                throw systemErrorFromErrno("listen() failed");
            }
            listeningThread = std::thread(listener);
        }

        ListeningSocket(const ListeningSocket &src) = delete;
        ListeningSocket &operator=(const ListeningSocket &) = delete;

        ListeningSocket(ListeningSocket &&src) noexcept :
                socketAddress(std::exchange(src.socketAddress, {})),
                listeningThread(std::exchange(src.listeningThread, {})),
                listen_fd(std::exchange(src.listen_fd, -1)) {}

        ListeningSocket& operator=(ListeningSocket &&rhs) noexcept {
            ListeningSocket s(std::move(rhs));
            std::swap(socketAddress, s.socketAddress);
            std::swap(listeningThread, s.listeningThread);
            std::swap(listen_fd, s.listen_fd);
            return *this;
        }

        [[nodiscard]] int nextClientFd() { //NOLINT (is not const: socket changes)
            auto client_fd = accept(listen_fd, nullptr, nullptr);
            if (client_fd < 0) {
                throw systemErrorFromErrno("accept() failed");
            }
            return client_fd;
        }

        void listen(const std::function<void()>& listener) {
            listeningThread = std::thread(listener);
        };

        ~ListeningSocket() {
            shutdown(listen_fd, SHUT_RDWR);
            close(listen_fd);
            listeningThread.join();
            unlink(socketAddress.c_str());
        }
    };

    template<typename ListenConnT, typename ClientConnT>
    class BasicServer {
    private:
        std::unique_ptr<ListenConnT> serviceConnection;
        Service service;

        void acceptLoop() {
            for (;;) {
                try {
                    std::thread{[this](int fd) {
                        clientLoop(ClientConnT(fd));
                    }, serviceConnection->nextClientFd()}.detach();
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
                } catch (std::invalid_argument &e) {
                    std::cerr << "Invalid message: " << e.what() << std::endl;
                    break;
                } catch (std::system_error &e) {
                    if (e.code() != std::error_code(0, std::system_category())) {
                        std::cerr << "Terminate connection: " << e.what() << std::endl;
                    }
                    break;
                }
            }
        }

    public:
        BasicServer(const std::string &address, const std::string &vendor, const std::string &product,
                    const std::string &version, const std::string &url)
                : serviceConnection(std::make_unique<ListenConnT>(address, [this]() { acceptLoop(); })),
                  service(vendor, product, version, url) {}

        explicit BasicServer(std::unique_ptr<ListenConnT> listenConn)
                : serviceConnection(std::move(listenConn)), service() {
            serviceConnection->listen([this]() { acceptLoop(); });
        }

        /*BasicServer(BasicServer &&src) noexcept :
                listeningThread(std::exchange(src.listeningThread, {})) {}

        BasicServer& operator=(BasicServer &&rhs) noexcept {
            BasicServer s(std::move(rhs));
            std::swap(listeningThread, s.listeningThread);
            return *this;
        }*/

        void addInterface(const Interface &interface) { service.addInterface(interface); }

        void addInterface(std::string_view interface, const CallbackMap &callbacks) {
            service.addInterface(Interface(interface, callbacks));
        }

    };

    using ThreadedServer = BasicServer<ListeningSocket, SocketConnection>;
}

#endif