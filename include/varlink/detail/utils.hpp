#ifndef LIBVARLINK_UTILS_HPP
#define LIBVARLINK_UTILS_HPP

namespace varlink::detail {
template <std::size_t SubIdx, std::size_t... Offsets, typename Tuple, std::size_t SplitSize = sizeof...(Offsets)>
constexpr auto get_sub_tuple(const Tuple& tuple)
{
    static_assert(std::tuple_size_v<Tuple> >= (SubIdx + 1) * SplitSize, "Out of bounds");
    return std::forward_as_tuple(std::get<SubIdx * SplitSize + Offsets>(tuple)...);
}

template <typename Tuple, std::size_t... Idx, std::size_t... Offsets>
constexpr auto tuple_split(const Tuple& tuple, std::index_sequence<Idx...>, std::index_sequence<Offsets...>)
{
    static_assert(std::tuple_size_v<Tuple> % sizeof...(Idx) == 0, "Uneven split");
    return std::make_tuple(get_sub_tuple<Idx, Offsets...>(tuple)...);
}

template <typename Tuple, std::size_t... Offsets>
constexpr auto tuple_split(const Tuple&, std::index_sequence<>, std::index_sequence<Offsets...>)
{
    static_assert(std::tuple_size_v<Tuple> == 0);
    return std::make_tuple();
}

template <std::size_t SplitSize, typename Tuple>
constexpr auto tuple_split(Tuple&& tuple)
{
    return tuple_split(
        std::forward<Tuple>(tuple),
        std::make_index_sequence<std::tuple_size_v<Tuple> / SplitSize>{},
        std::make_index_sequence<SplitSize>{});
}

template <std::size_t SplitSize, typename... Args>
constexpr auto make_tuples(Args&&... args)
{
    return tuple_split<SplitSize>(std::forward_as_tuple(args...));
}

template <typename Type, typename>
struct is_tuple_constructible;

template <typename Type, typename... Args>
struct is_tuple_constructible<Type, std::tuple<Args...>> : std::is_constructible<Type, Args...> {
};

template <typename Type, typename Tuple>
static constexpr auto is_tuple_constructible_v = is_tuple_constructible<Type, Tuple>::value;

static_assert(std::is_constructible_v<std::string, std::string_view>);
static_assert(is_tuple_constructible_v<std::string, std::tuple<std::string_view>>);
} // namespace varlink::detail
#endif // LIBVARLINK_UTILS_HPP
