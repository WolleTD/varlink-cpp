#include <sstream>
#include <org.varlink.service.varlink.hpp>
#include <varlink/service.hpp>

namespace varlink {
varlink_service::varlink_service(description Description) : desc(std::move(Description))
{
    auto getInfo = [this] varlink_callback {
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
    auto getInterfaceDescription = [this] varlink_callback {
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
}
