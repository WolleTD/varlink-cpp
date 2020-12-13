#ifndef LIBVARLINK_CONFIG_HPP
#define LIBVARLINK_CONFIG_HPP

#define LIBVARLINK_HAS_BOOST_ASIO __has_include(<boost/asio.hpp>)
#define LIBVARLINK_HAS_ASIO __has_include(<asio.hpp>)

#if not defined(LIBVARLINK_USE_BOOST) and not LIBVARLINK_HAS_ASIO and LIBVARLINK_HAS_BOOST_ASIO
#define LIBVARLINK_USE_BOOST 1
#endif

#if defined(LIBVARLINK_USE_BOOST)
#include <boost/asio.hpp>
#else
#include <asio.hpp>
#endif

namespace varlink {

#if defined(LIBVARLINK_USE_BOOST)
namespace net = ::boost::asio;
#else
namespace net = ::asio;
#endif

#if defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN)
#define VARLINK_DEFAULT_COMPLETION_TOKEN(e) \
    BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(e)
#elif defined(ASIO_DEFAULT_COMPLETION_TOKEN)
#define VARLINK_DEFAULT_COMPLETION_TOKEN(e) ASIO_DEFAULT_COMPLETION_TOKEN(e)
#elif defined(VARLINK_DOCUMENTATION)
#define VARLINK_DEFAULT_COMPLETION_TOKEN(e) \
    = typename net::default_completion_token<e>::type()
#else
#define VARLINK_DEFAULT_COMPLETION_TOKEN(e)
#endif // defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN)

#if defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)
#define VARLINK_DEFAULT_COMPLETION_TOKEN_TYPE(e) \
    BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#elif defined(ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)
#define VARLINK_DEFAULT_COMPLETION_TOKEN_TYPE(e) \
    ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#elif defined(VARLINK_DOCUMENTATION)
#define VARLINK_DEFAULT_COMPLETION_TOKEN(e) \
    = typename net::default_completion_token<e>::type
#else
#define VARLINK_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#endif // defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)

#if defined(BOOST_ASIO_COMPLETION_TOKEN_FOR)
#define VARLINK_COMPLETION_TOKEN_FOR(s) BOOST_ASIO_COMPLETION_TOKEN_FOR(s)
#elif defined(ASIO_COMPLETION_TOKEN_FOR)
#define VARLINK_COMPLETION_TOKEN_FOR(s) ASIO_COMPLETION_TOKEN_FOR(s)
#else
#define VARLINK_COMPLETION_TOKEN_FOR(s) typename
#endif // defined (BOOST_ASIO_COMPLETION_TOKEN_FOR)

}
#endif // LIBVARLINK_CONFIG_HPP
