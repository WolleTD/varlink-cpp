#include <sstream>
#include <org.varlink.service.varlink.hpp>
#include <varlink/service.hpp>

template <typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

namespace varlink {
varlink_service::interface::interface(std::string_view definition, callback_map callbacks)
    : interface(varlink_interface(definition), std::move(callbacks))
{
}

varlink_service::interface::interface(varlink_interface spec, callback_map callbacks)
    : spec_(std::move(spec)), callbacks_(std::move(callbacks))
{
    for (auto& callback : callbacks_) {
        if (not spec_.has_method(callback.first)) {
            throw std::invalid_argument("Callback for unknown method");
        }
    }
}

void varlink_service::interface::add_callback(const std::string& methodname, callback_function fn)
{
    if (callbacks_.find(methodname) != callbacks_.end()) {
        throw std::invalid_argument("Callback already set");
    }
    if (not spec_.has_method(methodname)) {
        throw std::invalid_argument("Callback for unknown method");
    }
    callbacks_[methodname] = std::move(fn);
}

[[nodiscard]] auto varlink_service::interface::callback(const std::string& methodname) const
    -> const callback_function&
{
    const auto callback_entry = callbacks_.find(methodname);
    if (callback_entry == callbacks_.end()) throw std::bad_function_call{};
    return callback_entry->second;
}

varlink_service::varlink_service(description Description) : desc(std::move(Description))
{
    auto getInfo = [this](const json&, callmode) {
        json::object_t info = {
            {"vendor", desc.vendor},
            {"product", desc.product},
            {"version", desc.version},
            {"url", desc.url}};
        info["interfaces"] = json::array();
        for (const auto& interface : interfaces) {
            info["interfaces"].push_back(interface->name());
        }
        return info;
    };
    auto getInterfaceDescription = [this](const json& parameters, callmode) -> json {
        const auto& ifname = parameters["interface"].get<std::string>();

        if (const auto interface_it = find_interface(ifname); interface_it != interfaces.cend()) {
            std::stringstream ss;
            ss << **interface_it;
            return {{"description", ss.str()}};
        }
        else {
            throw varlink_error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
        }
    };
    add_interface(
        org_varlink_service_varlink,
        {{"GetInfo", getInfo}, {"GetInterfaceDescription", getInterfaceDescription}});
}

void varlink_service::message_call(
    const basic_varlink_message& message,
    reply_function&& replySender) const noexcept
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
        auto visitor = overloaded{
            [&](const sync_callback_function& sc) -> void {
                auto params = sc(message.parameters(), message.mode());
                interface->validate(params, m.method_return_type());
                if (message.mode() == callmode::oneway)
                    replySender(nullptr, nullptr);
                else
                    replySender({{"parameters", params}}, nullptr);
            },
            [&](const async_callback_function& mc) -> void {
                // This is not an asynchronous callback and exceptions
                // will propagate up to the outer try-catch in this fn.
                // TODO: This isn't true if the callback dispatches async ops
                auto handler = [mode = message.mode(),
                                interface,
                                &return_type = m.method_return_type(),
                                replySender = std::move(replySender)](
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
            [](const nullptr_t&) -> void { throw std::bad_function_call{}; }};
        std::visit(visitor, callback);
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

void varlink_service::add_interface(interface&& interf)
{
    if (auto pos = find_interface(interf->name()); pos == interfaces.end()) {
        interfaces.push_back(std::move(interf));
    }
    else {
        throw std::invalid_argument("Interface already exists!");
    }
}

void varlink_service::add_interface(varlink_interface&& spec, callback_map&& callbacks)
{
    add_interface(interface{std::move(spec), std::move(callbacks)});
}

void varlink_service::add_interface(std::string_view definition, callback_map&& callbacks)
{
    add_interface(interface{definition, std::move(callbacks)});
}

[[nodiscard]] auto varlink_service::find_interface(std::string_view ifname) const
    -> std::vector<interface>::const_iterator
{
    return std::find_if(interfaces.cbegin(), interfaces.cend(), [&ifname](auto& i) {
        return (ifname == i->name());
    });
}
}
