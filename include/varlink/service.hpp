#ifndef LIBVARLINK_SERVICE_HPP
#define LIBVARLINK_SERVICE_HPP

#include <mutex>
#include <varlink/detail/message.hpp>
#include <varlink/detail/org.varlink.service.varlink.hpp>
#include <varlink/interface.hpp>

namespace varlink {
class varlink_service {
  public:
    struct description {
        std::string vendor{};
        std::string product{};
        std::string version{};
        std::string url{};
    };

  private:
    description desc;
    std::mutex interfaces_mut;
    std::vector<varlink_interface> interfaces{};

    auto find_interface(const std::string& ifname)
    {
        auto lock = std::lock_guard(interfaces_mut);
        return std::find_if(interfaces.cbegin(), interfaces.cend(), [&ifname](auto& i) {
            return (ifname == i.name());
        });
    }

  public:
    explicit varlink_service(description Description) : desc(std::move(Description))
    {
        auto getInfo = [this] varlink_callback {
            json::object_t info = {
                {"vendor", desc.vendor},
                {"product", desc.product},
                {"version", desc.version},
                {"url", desc.url}};
            info["interfaces"] = json::array();
            auto lock = std::lock_guard(interfaces_mut);
            for (const auto& interface : interfaces) {
                info["interfaces"].push_back(interface.name());
            }
            send_reply(info, false);
        };
        auto getInterfaceDescription = [this] varlink_callback {
            const auto& ifname = parameters["interface"].get<std::string>();

            if (const auto interface = find_interface(ifname); interface != interfaces.cend()) {
                std::stringstream ss;
                ss << *interface;
                send_reply({{"description", ss.str()}}, false);
            }
            else {
                throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            }
        };
        add_interface(
            org_varlink_service_varlink,
            "GetInfo" >> getInfo,
            "GetInterfaceDescription" >> getInterfaceDescription);
    }

    varlink_service(const varlink_service& src) = delete;
    varlink_service& operator=(const varlink_service&) = delete;
    varlink_service(varlink_service&& src) = delete;
    varlink_service& operator=(varlink_service&&) = delete;

    template <typename ReplyHandler>
    void message_call(const basic_varlink_message& message, ReplyHandler&& replySender) noexcept
    {
        const auto error = [&](const std::string& what, const json& params) {
            assert(params.is_object());
            replySender({{"error", what}, {"parameters", params}}, false);
        };
        const auto [ifname, methodname] = message.interface_and_method();
        const auto interface = find_interface(ifname);
        if (interface == interfaces.cend()) {
            error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            return;
        }

        try {
            const auto& method = interface->method(methodname);
            interface->validate(message.parameters(), method.parameters);

            method.callback(
                message.parameters(),
                message.more(),
                // This is not an asynchronous callback and exceptions
                // will propagate up to the outer try-catch in this fn.
                // TODO: This isn't true if the callback dispatches async ops
                [oneway = message.oneway(),
                 more = message.more(),
                 &interface = *interface,
                 &method,
                 replySender = std::forward<ReplyHandler>(replySender)](
                    const json::object_t& params, bool continues) mutable {
                    interface.validate(params, method.return_value);

                    if (oneway) { replySender(nullptr, false); }
                    else if (more) {
                        replySender({{"parameters", params}, {"continues", continues}}, continues);
                    }
                    else if (continues) { // and not more
                        throw std::bad_function_call{};
                    }
                    else {
                        replySender({{"parameters", params}}, false);
                    }
                });
        }
        catch (std::out_of_range& e) {
            error("org.varlink.service.MethodNotFound", {{"method", ifname + '.' + methodname}});
        }
        catch (std::bad_function_call& e) {
            error("org.varlink.service.MethodNotImplemented", {{"method", ifname + '.' + methodname}});
        }
        catch (varlink_error& e) {
            error(e.what(), e.args());
        }
        catch (std::exception& e) {
            error("org.varlink.service.InternalError", {{"what", e.what()}});
        }
    }

    void add_interface(varlink_interface&& interface)
    {
        if (auto pos = find_interface(interface.name()); pos == interfaces.end()) {
            auto lock = std::lock_guard(interfaces_mut);
            interfaces.push_back(std::move(interface));
        }
        else {
            throw std::invalid_argument("Interface already exists!");
        }
    }

    template <typename... Args>
    void add_interface(std::string_view definition, Args&&... args)
    {
        add_interface(varlink_interface(definition, std::forward<Args>(args)...));
    }
};
} // namespace varlink
#endif // LIBVARLINK_SERVICE_HPP
