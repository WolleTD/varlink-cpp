#ifndef LIBVARLINK_CONFIG_HPP
#define LIBVARLINK_CONFIG_HPP

#define LIBVARLINK_HAS_BOOST_ASIO __has_include(<boost/asio.hpp>)
#define LIBVARLINK_HAS_ASIO __has_include(<asio.hpp>)

#if not defined(LIBVARLINK_USE_BOOST) and not LIBVARLINK_HAS_ASIO and LIBVARLINK_HAS_BOOST_ASIO
#define LIBVARLINK_USE_BOOST 1
#endif

#if defined(LIBVARLINK_USE_BOOST)
#include <boost/asio/error.hpp>
#else
#include <asio/error.hpp>
#endif

namespace varlink {

#if defined(LIBVARLINK_USE_BOOST)
namespace net = ::boost::asio;
using error_code = boost::system::error_code;
using system_error = boost::system::system_error;
#else
namespace net = ::asio;
using error_code = std::error_code;
using system_error = std::system_error;
#endif

}
#endif // LIBVARLINK_CONFIG_HPP
