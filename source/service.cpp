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
        if (spec_.find_method(callback.first) == spec_.end()) {
            throw std::invalid_argument("Callback for unknown method");
        }
    }
}

void varlink_service::interface::add_callback(const std::string& methodname, callback_function fn)
{
    if (callbacks_.find(methodname) != callbacks_.end()) {
        throw std::invalid_argument("Callback already set");
    }
    if (spec_.find_method(methodname) == spec_.end()) {
        throw std::invalid_argument("Callback for unknown method");
    }
    callbacks_[methodname] = std::move(fn);
}

[[nodiscard]] auto varlink_service::interface::callback(const std::string& methodname) const
    -> const callback_function&
{
    static callback_function null_callback = nullptr;

    const auto callback_entry = callbacks_.find(methodname);
    if (callback_entry == callbacks_.end()) return null_callback;
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
        const auto ifname = parameters["interface"].get<std::string>();

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

struct more_reply_handler {
    more_reply_handler(
        const basic_varlink_message& message,
        const varlink_interface& interface,
        const detail::member& method,
        reply_function&& replySender)
        : mode(message.mode()),
          method(message.full_method()),
          interface(interface),
          return_type(method.method_return_type()),
          replySender(std::move(replySender))
    {
    }

    void operator()(std::exception_ptr eptr, const json& params, more_handler&& handler) const
    {
        auto error = [&](const std::string& what, const json& params2) {
            assert(params.is_object());
            replySender(eptr, {{"error", what}, {"parameters", params2}}, std::move(handler));
        };

        try {
            interface.validate(params, return_type);
        }
        catch (invalid_parameter& e) {
            eptr = std::current_exception();
            error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
            return;
        }

        if (mode == callmode::oneway) { replySender(eptr, nullptr, nullptr); }
        else if (mode == callmode::more) {
            const json reply = {{"parameters", params}, {"continues", static_cast<bool>(handler)}};
            replySender(eptr, reply, std::move(handler));
        }
        else if (handler) { // and not more
            eptr = std::make_exception_ptr(std::bad_function_call());
            error("org.varlink.service.MethodNotImplemented", {{"method", method}});
        }
        else {
            replySender(eptr, {{"parameters", params}}, nullptr);
        }
    }

  private:
    callmode mode;
    std::string method;
    const varlink_interface& interface;
    const detail::type_spec& return_type;
    reply_function replySender;
};

static void process_call(
    const basic_varlink_message& message,
    const varlink_service::interface& interface,
    const detail::member& method,
    reply_function&& replySender)
{
    const auto error = [=](const std::string& what, const json& params) {
        assert(params.is_object());
        replySender(std::current_exception(), {{"error", what}, {"parameters", params}}, nullptr);
    };

    try {
        auto visitor = overloaded{
            [&](const sync_callback_function& sc) -> void {
                auto params = sc(message.parameters(), message.mode());
                interface->validate(params, method.method_return_type());
                if (message.mode() == callmode::oneway)
                    replySender({}, nullptr, nullptr);
                else
                    replySender({}, {{"parameters", params}}, nullptr);
            },
            [&](const async_callback_function& mc) -> void {
                auto handler = more_reply_handler(
                    message, *interface, method, std::move(replySender));
                mc(message.parameters(), message.mode(), handler);
            },
            [](const nullptr_t&) -> void { throw std::bad_function_call{}; }};

        auto& callback = interface.callback(message.method());
        std::visit(visitor, callback);
    }
    catch (std::bad_function_call&) {
        error("org.varlink.service.MethodNotImplemented", {{"method", message.full_method()}});
    }
    catch (invalid_parameter& e) {
        error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
    }
    catch (varlink_error& e) {
        error(e.type(), e.params());
    }
    catch (std::exception& e) {
        error("org.varlink.service.InternalError", {{"what", e.what()}});
    }
}

void varlink_service::message_call(const basic_varlink_message& message, reply_function&& replySender) const
{
    const auto error = [=](const std::string& what, const json& params) {
        assert(params.is_object());
        replySender({}, {{"error", what}, {"parameters", params}}, nullptr);
    };
    const auto ifname = message.interface();
    const auto methodname = message.method();
    const auto interface_it = find_interface(ifname);
    if (interface_it == interfaces.cend()) {
        error("org.varlink.service.InterfaceNotFound", {{"interface", ifname}});
        return;
    }
    const auto& interface = *interface_it;

    const auto method_it = interface->find_method(methodname);
    if (method_it == interface->end()) {
        error("org.varlink.service.MethodNotFound", {{"method", message.full_method()}});
        return;
    }
    const auto& method = *method_it;

    try {
        interface->validate(message.parameters(), method.method_parameter_type());
    }
    catch (invalid_parameter& e) {
        error("org.varlink.service.InvalidParameter", {{"parameter", e.what()}});
        return;
    }

    process_call(message, interface, method, std::move(replySender));
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
