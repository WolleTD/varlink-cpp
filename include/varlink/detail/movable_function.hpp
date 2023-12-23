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
class movable_function : public std::function<T> {
    template <typename Fn, typename En = void>
    struct wrapper {};

    // specialization for CopyConstructible Fn
    template <typename Fn>
    struct wrapper<Fn, std::enable_if_t<std::is_copy_constructible_v<Fn>>> {
        Fn fn;

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn(std::forward<Args>(args)...);
        }
    };

    // specialization for MoveConstructible-only Fn
    template <typename Fn>
    struct wrapper<Fn, std::enable_if_t<!std::is_copy_constructible_v<Fn> && std::is_move_constructible_v<Fn>>> {
        Fn fn;

        explicit wrapper(Fn&& _fn) : fn(std::forward<Fn>(_fn)) {}

        wrapper(wrapper&&) = default;
        wrapper& operator=(wrapper&&) = default;

        // these two functions are instantiated by std::function and are never called
        wrapper(const wrapper& rhs) : fn(const_cast<Fn&&>(rhs.fn))
        {
            // hack to initialize fn for non-DefaultContructible types
            std::abort();
        }
        wrapper& operator=(const wrapper&) { std::abort(); }

        template <typename... Args>
        auto operator()(Args&&... args)
        {
            return fn(std::forward<Args>(args)...);
        }
    };

    using base = std::function<T>;

  public:
    movable_function() noexcept = default;
    movable_function(std::nullptr_t) noexcept : base(nullptr) {}

    template <typename Fn>
    movable_function(Fn&& f) : base(wrapper<Fn>{std::forward<Fn>(f)})
    {
    }

    movable_function(movable_function&&) noexcept = default;
    movable_function& operator=(movable_function&&) noexcept = default;

    movable_function& operator=(std::nullptr_t)
    {
        base::operator=(nullptr);
        return *this;
    }

    template <typename Fn>
    movable_function& operator=(Fn&& f)
    {
        base::operator=(wrapper<Fn>{std::forward<Fn>(f)});
        return *this;
    }

    using base::operator();
};
} // namespace varlink::detail

#endif
