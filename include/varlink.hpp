/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>

#define VarlinkCallback \
    ([[maybe_unused]] const varlink::json& message, [[maybe_unused]] varlink::Connection& connection) -> varlink::json

namespace varlink {
    using nlohmann::json;

    class Connection {
    private:
        __gnu_cxx::stdio_filebuf<char> filebuf_in;
        __gnu_cxx::stdio_filebuf<char> filebuf_out;

        std::istream rstream { &filebuf_in };
        std::ostream wstream { &filebuf_out };

        int socket_fd { -1 };
    public:
        // Connect to a service via address
        explicit Connection(const std::string& address);

        // Setup message stream on existing connection
        explicit Connection(int posix_fd);

        Connection(const Connection& src) = delete;
        Connection(Connection&& src) noexcept;

        void send(const json& message);

        [[nodiscard]] json receive();
    };

    using MethodCallback = std::function<json(const json&, Connection& connection)>;

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
        std::string description;

        std::map<std::string, Type> types;
        std::map<std::string, Method> methods;
        std::map<std::string, Error> errors;

        template<typename Rule>
        struct inserter {};

        struct {
            std::string moving_docstring {};
            std::string docstring {};
            std::string name {};
            std::map<std::string, MethodCallback> callbacks {};
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
        explicit Interface(std::string fromDescription,
                           std::map<std::string, MethodCallback> callbacks = {});
        [[nodiscard]] const std::string& name() const noexcept { return ifname; }
        [[nodiscard]] const Method& method(const std::string& name) const;
        [[nodiscard]] json validate(const json& data, const json& type) const;

        friend std::ostream& operator<<(std::ostream& os, const Interface& interface);
    };

    std::ostream& operator<<(std::ostream& os, const Type& type);
    std::ostream& operator<<(std::ostream& os, const Error& error);
    std::ostream& operator<<(std::ostream& os, const Method& method);
    std::ostream& operator<<(std::ostream& os, const Interface& interface);
    std::string element_to_string(const json& type, size_t indent = 0);
    std::string vtype_to_string(const json& type);

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
        Service(Service&& src) noexcept;
        ~Service();

        [[nodiscard]] Connection nextClientConnection() const;
        void addInterface(Interface interface) { interfaces.emplace(interface.name(), std::move(interface)); }
    };

    inline json reply(json params) {
        return {{"parameters", std::move(params)}};
    }
    inline json reply_continues(json params, bool continues = true) {
        return {{"parameters", std::move(params)}, {"continues", continues}};
    }
    inline json error(std::string what, json params) {
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
