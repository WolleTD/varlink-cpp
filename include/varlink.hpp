/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <unordered_map>
#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>
#include <utility>
#include <ext/stdio_filebuf.h>

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& message, \
     [[maybe_unused]] varlink::Connection& connection, \
     [[maybe_unused]] bool more) -> varlink::json

#define VarlinkTemplateCallback \
    ([[maybe_unused]] const varlink::json& message, \
     [[maybe_unused]] ConnectionT& connection, \
     [[maybe_unused]] bool more) -> varlink::json

namespace varlink {
    using nlohmann::json;

    class Connection {
    private:
        int socket_fd { -1 };
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;

        std::istream rstream { &filebuf_in };
        std::ostream wstream { &filebuf_out };
    public:
        // Connect to a service via address
        explicit Connection(const std::string& address);

        // Setup message stream on existing connection
        explicit Connection(int posix_fd);

        Connection(const Connection& src) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& src) noexcept;
        Connection& operator=(Connection&& rhs) noexcept;

        void send(const json& message);

        [[nodiscard]] json receive();
    };

    using MethodCallback = std::function<json(const json&, Connection& connection, bool more)>;
    using CallbackMap = std::unordered_map<std::string, MethodCallback>;

    struct Type {
        const std::string name;
        const std::string description;
        const json data;

        friend std::ostream& operator<<(std::ostream& os, const Type& type);
    };

    struct Error : Type {
        friend std::ostream& operator<<(std::ostream& os, const Error& error);
    };

    struct Method {
        const std::string name;
        const std::string description;
        const json parameters;
        const json returnValue;
        const MethodCallback callback;

        friend std::ostream& operator<<(std::ostream& os, const Method& method);
    };

    class Interface {
    private:
        std::string ifname;
        std::string documentation;
        std::string_view description;

        std::vector<Type> types;
        std::vector<Method> methods;
        std::vector<Error> errors;

        template<typename Rule>
        struct inserter {};

        struct {
            std::string moving_docstring {};
            std::string docstring {};
            std::string name {};
            CallbackMap callbacks {};
            json method_params {};
        } state;

        struct State {
            std::vector<std::string> fields {};
            size_t pos { 0 };
            json last_type {};
            json last_element_type {};
            json work {};
            bool maybe_type { false };
            bool dict_type { false };
            bool array_type { false };
        };
        std::vector<State> stack;

    public:
        explicit Interface(std::string_view fromDescription,
                           CallbackMap callbacks = {});
        [[nodiscard]] const std::string& name() const noexcept { return ifname; }
        [[nodiscard]] const std::string& doc() const noexcept { return documentation; }
        [[nodiscard]] const Method& method(const std::string& name) const;
        [[nodiscard]] const Type& type(const std::string& name) const;
        [[nodiscard]] const Error& error(const std::string& name) const;
        [[nodiscard]] bool has_method(const std::string& name) const noexcept;
        [[nodiscard]] bool has_type(const std::string& name) const noexcept;
        [[nodiscard]] bool has_error(const std::string& name) const noexcept;
        void validate(const json& data, const json& type) const;
        json call(const std::string& method, const json& parameters);

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0);

    inline json reply(json params) {
        assert(params.is_object());
        return {{"parameters", std::move(params)}};
    }
    inline json reply_continues(json params, bool continues = true) {
        assert(params.is_object());
        return {{"parameters", std::move(params)}, {"continues", continues}};
    }
    inline json error(std::string what, json params) {
        assert(params.is_object());
        return {{"error", std::move(what)}, {"parameters", std::move(params)}};
    }

    std::string_view org_varlink_service_description();

    class ServiceConnection {
    private:
        std::string socketAddress;
        std::thread listeningThread;
        int listen_fd { -1 };
        std::function<void(int)> connectionCallback;
    public:
        explicit ServiceConnection(std::string address, std::function<void(int)> callback);
        ServiceConnection(const ServiceConnection& src) = delete;
        ServiceConnection& operator=(const ServiceConnection&) = delete;
        ServiceConnection(ServiceConnection&& src) noexcept;
        ServiceConnection& operator=(ServiceConnection&& rhs) noexcept;
        ~ServiceConnection();

        [[nodiscard]] int nextClientFd();
    };

    template<typename ConnectionT, typename InterfaceT>
    class BasicService {
    private:
        ServiceConnection serviceConnection;
        std::string serviceVendor;
        std::string serviceProduct;
        std::string serviceVersion;
        std::string serviceUrl;
        std::map<std::string, InterfaceT> interfaces;

        json handle(const json &message, ConnectionT &connection) {
            const auto &fqmethod = message["method"].get<std::string>();
            const auto dot = fqmethod.rfind('.');
            if (dot == std::string::npos) {
                return error("org.varlink.service.InterfaceNotFound", {{"interface", fqmethod}});
            }
            const auto ifname = fqmethod.substr(0, dot);
            const auto methodname = fqmethod.substr(dot + 1);

            try {
                const auto& interface = interfaces.at(ifname);
                try {
                    const auto &method = interface.method(methodname);
                    interface.validate(message["parameters"], method.parameters);
                    const bool more = (message.contains("more") && message["more"].get<bool>());
                    auto response = method.callback(message, connection, more);
                    try {
                        interface.validate(response["parameters"], method.returnValue);
                    } catch(std::invalid_argument& e) {
                        std::cout << "Response validation error: " << e.what() << std::endl;
                    }
                    return response;
                }
                catch (std::out_of_range& e) {
                    return error("org.varlink.service.MethodNotFound", {{"method", methodname}});
                }
                catch (std::invalid_argument& e) {
                    return error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
                }
                catch (std::bad_function_call& e) {
                    return error("org.varlink.service.MethodNotImplemented", {{"method", methodname}});
                }
            }
            catch (std::out_of_range& e) {
                return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            }
        }

        void clientLoop(ConnectionT conn) {
            for (;;) {
                try {
                    json message = conn.receive();
                    if (message.is_null() || !message.contains("method")) break;
                    message.merge_patch(R"({"parameters":{}})"_json);
                    auto reply = handle(message, conn);
                    if (!(message.contains("oneway") && message["oneway"].get<bool>())) {
                        conn.send(reply);
                    }
                } catch(std::system_error& e) {
                    std::cerr << "Terminate connection: " << e.what() << std::endl;
                    break;
                }
            }
        }

    public:
        BasicService(const std::string& address, std::string  vendor, std::string  product,
                     std::string  version, std::string  url)
                     : serviceConnection(address, [this](int fd) {
                         clientLoop(ConnectionT(fd));
                     }),
                     serviceVendor{std::move(vendor)}, serviceProduct{std::move(product)},
                     serviceVersion{std::move(version)}, serviceUrl{std::move(url)} {
            addInterface(org_varlink_service_description(), {
                {"GetInfo", [this]VarlinkTemplateCallback {
                    json info = {
                        {"vendor", serviceVendor},
                        {"product", serviceProduct},
                        {"version", serviceVersion},
                        {"url", serviceUrl}
                    };
                    info["interfaces"] = json::array();
                    for(const auto& interface : interfaces) {
                        info["interfaces"].push_back(interface.first);
                    }
                    return reply(info);
                }},
                {"GetInterfaceDescription", [this]VarlinkTemplateCallback {
                    const auto& ifname = message["parameters"]["interface"].get<std::string>();
                    const auto interface = interfaces.find(ifname);
                    if (interface != interfaces.cend()) {
                        std::stringstream ss;
                        ss << interface->second;
                        return reply({{"description", ss.str()}});
                    } else {
                        return error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
                    }
                }}
            });
        }

        void addInterface(InterfaceT interface) { interfaces.emplace(interface.name(), std::move(interface)); }
        void addInterface(std::string_view interface, const CallbackMap& callbacks) {
            addInterface(InterfaceT(interface, callbacks));
        }
    };

    using Service = BasicService<Connection, Interface>;

    template<typename ConnectionT>
    class BasicClient {
    private:
        std::unique_ptr<ConnectionT> conn;
    public:
        enum class CallMode {
            Basic,
            Oneway,
            More,
            Upgrade,
        };
        explicit BasicClient(const std::string& address) : conn(std::make_unique<ConnectionT>(address)) {}
        explicit BasicClient(std::unique_ptr<ConnectionT> connection) : conn(std::move(connection)) {}

        std::function<json()> call(const std::string& method,
                                             const json& parameters,
                                             CallMode mode = CallMode::Basic) {
            json message { { "method", method } };
            if (!parameters.is_null() && !parameters.is_object()) {
                throw std::invalid_argument("parameters is not an object");
            }
            if (!parameters.empty()) {
                message["parameters"] = parameters;
            }

            if (mode == CallMode::Oneway) {
                message["oneway"] = true;
            } else if (mode == CallMode::More) {
                message["more"] = true;
            } else if (mode == CallMode::Upgrade) {
                message["upgrade"] = true;
            }

            conn->send(message);

            return [this, mode, continues = true]() mutable -> json {
                if (mode != CallMode::Oneway && continues) {
                    json reply = conn->receive();
                    if (mode == CallMode::More && reply.contains("continues")) {
                        continues = reply["continues"].get<bool>();
                    } else {
                        continues = false;
                    }
                    return reply;
                } else {
                    return {};
                }
            };
        }
    };

    using Client = BasicClient<Connection>;
}

#endif // LIBVARLINK_VARLINK_HPP
