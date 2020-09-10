/* Varlink C++ implementation using nlohmann/json as data format */
#ifndef LIBVARLINK_VARLINK_HPP
#define LIBVARLINK_VARLINK_HPP

#include "varlink/common.hpp"
#include "varlink/client.hpp"
#include "varlink/connection.hpp"
#include "varlink/interface.hpp"
#include "varlink/message.hpp"
#include "varlink/service.hpp"
#include "varlink/server.hpp"

namespace varlink {
    using ThreadedServer = BasicServer<ListeningSocket, Connection>;
    using Client = BasicClient<Connection>;
}

#endif // LIBVARLINK_VARLINK_HPP
