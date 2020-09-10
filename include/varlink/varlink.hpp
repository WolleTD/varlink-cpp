/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include "varlink/common.hpp"
#include "varlink/client.hpp"
#include "varlink/connection.hpp"
#include "varlink/interface.hpp"
#include "varlink/message.hpp"
#include "varlink/service.hpp"

namespace varlink {
    using Service = BasicService<ServiceConnection, Connection, Interface>;
    using Client = BasicClient<Connection>;
}

#endif // LIBVARLINK_VARLINK_HPP
