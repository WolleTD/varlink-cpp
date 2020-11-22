#ifndef LIBVARLINK_NL_JSON_HPP
#define LIBVARLINK_NL_JSON_HPP

#undef JSON_USE_IMPLICIT_CONVERSIONS
#define JSON_USE_IMPLICIT_CONVERSIONS 0
#include <nlohmann/json.hpp>

namespace varlink {
using nlohmann::json;
}
#endif // LIBVARLINK_NL_JSON_HPP
