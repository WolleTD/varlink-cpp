#ifndef LIBVARLINK_SERVICE_HPP
#define LIBVARLINK_SERVICE_HPP

#include <varlink/detail/message.hpp>
#include <varlink/detail/varlink_error.hpp>
#include <varlink/interface.hpp>

namespace varlink {

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using more_handler = std::function<void(std::error_code)>;
using reply_function = std::function<void(json::object_t, more_handler)>;

using simple_callback_function = std::function<json::object_t(const json&, callmode)>;
using more_callback_function = std::function<void(const json&, callmode, const reply_function&)>;
using callback_function = std::variant<nullptr_t, simple_callback_function, more_callback_function>;
using callback_map = std::map<std::string, callback_function>;

struct varlink_service {
    struct description {
        std::string vendor{};
        std::string product{};
        std::string version{};
        std::string url{};
    };

    explicit varlink_service(description Description);

    varlink_service(const varlink_service& src) = delete;
    varlink_service& operator=(const varlink_service&) = delete;
    varlink_service(varlink_service&& src) = delete;
    varlink_service& operator=(varlink_service&&) = delete;

  private:
    [[nodiscard]] auto find_interface(std::string_view ifname) const
    {
        return std::find_if(interfaces.cbegin(), interfaces.cend(), [&ifname](auto& i) {
            return (ifname == i->name());
        });
    }

  public:
    template <typename ReplyHandler>
    void message_call(const basic_varlink_message& message, ReplyHandler&& replySender) const noexcept
    {
        const auto error = [=](const std::string& what, const json& params) {
            assert(params.is_object());
            replySender({{"error", what}, {"parameters", params}}, nullptr);
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
            auto& callback = interface.callback(methodname);
            std::visit(
                overloaded{
                    [&](const simple_callback_function& sc) -> void {
                        auto params = sc(message.parameters(), message.mode());
                        interface->validate(params, m.method_return_type());
                        if (message.mode() == callmode::oneway)
                            replySender(nullptr, nullptr);
                        else
                            replySender({{"parameters", params}}, nullptr);
                    },
                    [&](const more_callback_function& mc) -> void {
                        // This is not an asynchronous callback and exceptions
                        // will propagate up to the outer try-catch in this fn.
                        // TODO: This isn't true if the callback dispatches async ops
                        auto handler = [mode = message.mode(),
                                        interface,
                                        &return_type = m.method_return_type(),
                                        replySender = std::forward<ReplyHandler>(replySender)](
                                           const json::object_t& params,
                                           const more_handler& continues) mutable {
                            interface->validate(params, return_type);

                            if (mode == callmode::oneway) { replySender(nullptr, nullptr); }
                            else if (mode == callmode::more) {
                                json reply = {{"parameters", params}, {"continues", bool(continues)}};
                                replySender(reply, continues);
                            }
                            else if (continues) { // and not more
                                throw std::bad_function_call{};
                            }
                            else {
                                replySender({{"parameters", params}}, nullptr);
                            }
                        };
                        mc(message.parameters(), message.mode(), handler);
                    },
                    [](const nullptr_t&) -> void { throw std::bad_function_call{}; }},
                callback);
        }
        catch (std::out_of_range&) {
            error("org.varlink.service.MethodNotFound", {{"method", ifname + '.' + methodname}});
        }
        catch (std::bad_function_call&) {
            error("org.varlink.service.MethodNotImplemented", {{"method", ifname + '.' + methodname}});
        }
        catch (invalid_parameter& e) {
            error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
        }
        catch (varlink_error& e) {
            error(e.what(), e.args());
        }
        catch (std::system_error&) {
            // All system_errors here are send-errors, so don't send anymore
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

  private:
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

    description desc;
    std::vector<interface_entry> interfaces{};
};
} // namespace varlink
#endif // LIBVARLINK_SERVICE_HPP
