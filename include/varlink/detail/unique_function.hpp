// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// originally from https://github.com/qchateau/packio

#ifndef LIBVARLINK_MOVABLE_FUNCTION_HPP
#define LIBVARLINK_MOVABLE_FUNCTION_HPP
#include <functional>
#include <type_traits>
#include <utility>

namespace varlink::detail {
template <typename T>
class unique_function : public std::function<T> {
    template <typename Fn, typename En = void>
    struct wrapper {
    };

    template <typename Fn>
    struct wrapper<Fn, std::enable_if_t<std::is_copy_constructible<Fn>::value>> {
        Fn fn_;

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn_(std::forward<Args>(args)...);
        }
    };

    template <typename Fn>
    struct wrapper<
        Fn,
        std::enable_if_t<
            !std::is_copy_constructible<Fn>::value
            && std::is_move_constructible<Fn>::value>> {
        Fn fn_;

        explicit wrapper(Fn&& fn) : fn_(std::forward<Fn>(fn)) {}

        wrapper(wrapper&&) noexcept = default;
        wrapper& operator=(wrapper&&) noexcept = default;

        // these two functions are instantiated by std::function
        // and are never called
        // the const_cast initialization is required for
        // non-DefaultContructible types to compile
        wrapper(const wrapper& rhs) : fn_(const_cast<Fn&&>(rhs.fn_))
        {
            std::abort();
        }
        wrapper& operator=(const wrapper&) { std::abort(); }

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn_(std::forward<Args>(args)...);
        }
    };

    using base = std::function<T>;

  public:
    unique_function() noexcept = default;
    explicit unique_function(std::nullptr_t) noexcept : base(nullptr) {}

    template <typename Fn, typename = std::enable_if_t<std::is_convertible_v<Fn, base>>>
    unique_function(Fn&& f) : base(wrapper<Fn>{std::forward<Fn>(f)})
    {
    }

    unique_function(unique_function&&) noexcept = default;
    unique_function& operator=(unique_function&&) noexcept = default;

    using base::operator();
};

} // namespace varlink::detail

#endif
