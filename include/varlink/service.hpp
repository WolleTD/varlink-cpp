#ifndef LIBVARLINK_SERVICE_HPP
#define LIBVARLINK_SERVICE_HPP

#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>
#include <varlink/interface.hpp>

#define varlink_callback                               \
    ([[maybe_unused]] const varlink::json& parameters, \
     [[maybe_unused]] varlink::callmode mode,          \
     [[maybe_unused]] const varlink::reply_function& send_reply)

namespace varlink {

using reply_function = std::function<void(json::object_t, bool)>;

using callback_function = std::function<void(const json&, callmode, const reply_function&)>;
using callback_map = std::map<std::string, callback_function>;

class varlink_service {
    struct interface_entry {
        interface_entry(varlink_interface spec, callback_map callbacks)
            : spec_(std::move(spec)), callbacks_(std::move(callbacks))
        {
        }

        auto* operator->() const { return &spec_; }
        auto& operator*() const { return spec_; }

        [[nodiscard]] auto& callback(const std::string& methodname) const
        {
            const auto callback_entry = callbacks_.find(methodname);
            if (callback_entry == callbacks_.end()) throw std::bad_function_call{};
            return callback_entry->second;
        }

      private:
        varlink_interface spec_;
        callback_map callbacks_;
    };

  public:
    struct description {
        std::string vendor{};
        std::string product{};
        std::string version{};
        std::string url{};
    };

  private:
    description desc;
    std::vector<interface_entry> interfaces{};

    [[nodiscard]] auto find_interface(std::string_view ifname) const
    {
        return std::find_if(interfaces.cbegin(), interfaces.cend(), [&ifname](auto& i) {
            return (ifname == i->name());
        });
    }

  public:
    explicit varlink_service(description Description);

    varlink_service(const varlink_service& src) = delete;
    varlink_service& operator=(const varlink_service&) = delete;
    varlink_service(varlink_service&& src) = delete;
    varlink_service& operator=(varlink_service&&) = delete;

    template <typename ReplyHandler>
    void message_call(const basic_varlink_message& message, ReplyHandler&& replySender) const noexcept
    {
        const auto error = [&](const std::string& what, const json& params) {
            assert(params.is_object());
            replySender({{"error", what}, {"parameters", params}});
        };
        const auto ifname = message.interface();
        const auto methodname = message.method();
        const auto interface_it = find_interface(ifname);
        if (interface_it == interfaces.cend()) {
            error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
            return;
        }

        try {
            const auto& interface = *interface_it;
            const auto& m = interface->method(methodname);
            interface->validate(message.parameters(), m.method_parameter_type());
            // This is not an asynchronous callback and exceptions
            // will propagate up to the outer try-catch in this fn.
            // TODO: This isn't true if the callback dispatches async ops
            auto handler = [mode = message.mode(),
                            interface,
                            &return_type = m.method_return_type(),
                            replySender = std::forward<ReplyHandler>(replySender)](
                               const json::object_t& params, bool continues) mutable {
                interface->validate(params, return_type);

                if (mode == callmode::oneway) { replySender(nullptr); }
                else if (mode == callmode::more) {
                    replySender({{"parameters", params}, {"continues", continues}});
                }
                else if (continues) { // and not more
                    throw std::bad_function_call{};
                }
                else {
                    replySender({{"parameters", params}});
                }
            };
            auto& callback = interface.callback(methodname);
            callback(message.parameters(), message.mode(), handler);
        }
        catch (std::out_of_range& e) {
            error("org.varlink.service.MethodNotFound", {{"method", ifname + '.' + methodname}});
        }
        catch (std::bad_function_call& e) {
            error("org.varlink.service.MethodNotImplemented", {{"method", ifname + '.' + methodname}});
        }
        catch (invalid_parameter& e) {
            error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
        }
        catch (varlink_error& e) {
            error(e.what(), e.args());
        }
        catch (std::exception& e) {
            error("org.varlink.service.InternalError", {{"what", e.what()}});
        }
    }

    void add_interface(varlink_interface&& interface, callback_map&& callbacks = {})
    {
        if (auto pos = find_interface(interface.name()); pos == interfaces.end()) {
            for (auto& callback : callbacks) {
                if (not interface.has_method(callback.first)) {
                    throw std::invalid_argument("Callback for unknown method");
                }
            }
            interfaces.emplace_back(std::move(interface), std::move(callbacks));
        }
        else {
            throw std::invalid_argument("Interface already exists!");
        }
    }

    void add_interface(std::string_view definition, callback_map&& callbacks = {})
    {
        add_interface(varlink_interface(definition), std::move(callbacks));
    }
};
} // namespace varlink
#endif // LIBVARLINK_SERVICE_HPP
