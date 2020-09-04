/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& message, \
     [[maybe_unused]] varlink::Connection& connection, \
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
        void validate(const json& data, const json& type) const;
        json call(const std::string& method, const json& parameters);

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const json& elem, int indent = 4, size_t depth = 0);

    class Service {
    public:
        struct Description {
            std::string vendor;
            std::string product;
            std::string version;
            std::string url;
        };
    private:
        std::string socketAddress;
        Description description;
        std::map<std::string, Interface> interfaces;
        std::thread listeningThread;
        int listen_fd { -1 };

        json handle(const json &message, Connection &connection);
        void dispatchConnections();
    public:
        Service(std::string address, Description desc);
        Service(const Service& src) = delete;
        Service& operator=(const Service&) = delete;
        Service(Service&& src) noexcept;
        Service& operator=(Service&& rhs) noexcept;
        ~Service();

        [[nodiscard]] Connection nextClientConnection() const;
        void addInterface(Interface interface) { interfaces.emplace(interface.name(), std::move(interface)); }
        void addInterface(std::string_view interface, CallbackMap callbacks);
    };

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

    inline void to_json(json& j, const Service::Description& desc) {
        j = json{
                {"vendor", desc.vendor},
                {"product", desc.product},
                {"version", desc.version},
                {"url", desc.url}
        };
    }

    class Client {
    private:
        Connection conn;
    public:
        enum class CallMode {
            Basic,
            Oneway,
            More,
            Upgrade,
        };
        explicit Client(const std::string& address);
        std::function<json()> call(const std::string& method,
                                             const json& parameters,
                                             CallMode mode = CallMode::Basic);
    };
}

#endif // LIBVARLINK_VARLINK_HPP
