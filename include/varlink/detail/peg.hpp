#ifndef LIBVARLINK_VARLINK_PEG_HPP
#define LIBVARLINK_VARLINK_PEG_HPP

#include <tao/pegtl.hpp>
#include <varlink/detail/nl_json.hpp>

namespace pegtl = tao::pegtl;

namespace grammar {
using namespace pegtl;

struct whitespace : one<' ', '\t'> {
};
struct eol_r : sor<one<'\r', '\n'>, string<'\r', '\n'>> {
};
struct comment : seq<one<'#'>, star<not_one<'\r', '\n'>>, eol_r> {
};
struct eol : sor<seq<star<whitespace>, eol_r>, comment> {
};
struct wce : sor<whitespace, comment, eol_r> {
};

struct field_name : seq<alpha, star<opt<one<'_'>>, alnum>> {
};
struct name : seq<range<'A', 'Z'>, star<alnum>> {
};
struct interface_name
    : seq<range<'a', 'z'>,
          star<star<one<'-'>>, ranges<'a', 'z', '0', '9'>>,
          plus<
              one<'.'>,
              ranges<'a', 'z', '0', '9'>,
              star<star<one<'-'>>, ranges<'a', 'z', '0', '9'>>>> {
};
struct array : string<'[', ']'> {
};
struct dict : string<'[', 's', 't', 'r', 'i', 'n', 'g', ']'> {
};
struct maybe : one<'?'> {
};

struct venum;
struct vstruct;
struct trivial_element_type : sor<string<'b', 'o', 'o', 'l'>,
                                  string<'i', 'n', 't'>,
                                  string<'f', 'l', 'o', 'a', 't'>,
                                  string<'s', 't', 'r', 'i', 'n', 'g'>,
                                  string<'o', 'b', 'j', 'e', 'c', 't'>,
                                  name> {
};
struct element_type : sor<trivial_element_type, venum, vstruct> {
};
struct type : sor<element_type,
                  seq<maybe, element_type>,
                  seq<array, type>,
                  seq<dict, type>,
                  seq<maybe, array, type>,
                  seq<maybe, dict, type>> {
};
struct object_start : one<'('> {
};
struct object_end : one<')'> {
};
struct venum : seq<object_start,
                   opt<list<seq<star<wce>, field_name, star<wce>>, one<','>>>,
                   star<wce>,
                   object_end> {
};
struct argument
    : seq<star<wce>, field_name, star<wce>, one<':'>, star<wce>, type> {
};
struct vstruct
    : seq<object_start, opt<list<seq<argument, star<wce>>, one<','>>>, star<wce>, object_end> {
};
struct kwtype : string<'t', 'y', 'p', 'e'> {
};
struct vtypedef : sor<seq<kwtype, plus<wce>, name, star<wce>, vstruct>,
                      seq<kwtype, plus<wce>, name, star<wce>, venum>> {
};
struct kwerror : string<'e', 'r', 'r', 'o', 'r'> {
};
struct error : seq<kwerror, plus<wce>, name, star<wce>, vstruct> {
};
struct kwmethod : string<'m', 'e', 't', 'h', 'o', 'd'> {
};
struct kwarrow : string<'-', '>'> {
};
struct method
    : seq<kwmethod, plus<wce>, name, star<wce>, vstruct, star<wce>, kwarrow, star<wce>, vstruct> {
};
struct member
    : sor<seq<star<wce>, method>, seq<star<wce>, vtypedef>, seq<star<wce>, error>> {
};
struct kwinterface : string<'i', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e'> {
};
struct interface : must<
                       star<wce>,
                       kwinterface,
                       plus<wce>,
                       interface_name,
                       eol,
                       list<member, eol>,
                       star<wce>> {
};

template <typename Rule>
struct inserter {
};

template <typename CallbackMap>
struct parser_state {
    struct {
        CallbackMap callbacks{};
        std::string moving_docstring{};
        std::string docstring{};
        std::string name{};
        nlohmann::json method_params{};
    } global;

    struct State {
        std::vector<std::string> fields{};
        size_t pos{0};
        nlohmann::json last_type{};
        nlohmann::json last_element_type{};
        nlohmann::json work{{"_order", nlohmann::json::array()}};
        bool maybe_type{false};
        bool dict_type{false};
        bool array_type{false};
        explicit State(size_t pos_ = 0) : pos(pos_) {}
    };
    std::vector<State> stack{};
    explicit parser_state(const CallbackMap& _callbacks) : global{_callbacks} {}
};

} // namespace grammar

#define INSERTER(match)                                                           \
    template <>                                                                   \
    struct grammar::inserter<match> {                                             \
        template <typename ParseInput, typename InterfaceT, typename CallbackMap> \
        static void apply(                                                        \
            const ParseInput& input,                                              \
            InterfaceT& interface,                                                \
            parser_state<CallbackMap>& pstate);                                   \
    };                                                                            \
    template <typename ParseInput, typename InterfaceT, typename CallbackMap>     \
    void grammar::inserter<match>::apply(                                         \
        [[maybe_unused]] const ParseInput& input,                                 \
        [[maybe_unused]] InterfaceT& interface,                                   \
        [[maybe_unused]] parser_state<CallbackMap>& pstate)

INSERTER(grammar::wce)
{
    if (*input.begin() == '#') {
        pstate.global.moving_docstring += input.string();
    }
    else if (*input.begin() == '\n') {
        pstate.global.moving_docstring.clear();
    }
}

INSERTER(grammar::maybe)
{
    auto& state = pstate.stack.back();
    state.maybe_type = true;
}

INSERTER(grammar::array)
{
    auto& state = pstate.stack.back();
    state.array_type = true;
}

INSERTER(grammar::dict)
{
    auto& state = pstate.stack.back();
    state.dict_type = true;
}

INSERTER(grammar::object_start)
{
    if (input.position().byte > pstate.stack.back().pos) {
        using StateT = typename grammar::parser_state<CallbackMap>::State;
        pstate.stack.emplace_back(StateT(input.position().byte));
    }
}

INSERTER(grammar::field_name)
{
    auto& state = pstate.stack.back();
    if (input.position().byte > state.pos) {
        state.fields.push_back(input.string());
        state.pos = input.position().byte;
    }
}

INSERTER(grammar::trivial_element_type)
{
    pstate.stack.back().last_element_type = input.string();
}

INSERTER(grammar::name)
{
    if (pstate.stack.size() == 1)
        pstate.global.name = input.string();
}

INSERTER(grammar::venum)
{
    auto state = pstate.stack.back();
    pstate.stack.pop_back();
    pstate.stack.back().last_element_type = state.fields;
}

INSERTER(grammar::argument)
{
    auto& state = pstate.stack.back();
    std::string key = state.fields.back();
    state.work[key] = state.last_type;
    state.work["_order"].push_back(key);
    state.last_type.clear();
    state.fields.pop_back();
}

INSERTER(grammar::vstruct)
{
    auto state = pstate.stack.back();
    pstate.stack.pop_back();
    assert(state.fields.empty());
    pstate.stack.back().last_element_type = state.work;
}

INSERTER(grammar::type)
{
    auto& state = pstate.stack.back();
    if (state.last_type.empty()) {
        state.last_type["type"] = state.last_element_type;
        if (state.dict_type) {
            state.last_type["dict_type"] = true;
            state.dict_type = false;
        }
        if (state.array_type) {
            state.last_type["array_type"] = true;
            state.array_type = false;
        }
        if (state.maybe_type) {
            state.last_type["maybe_type"] = true;
            state.maybe_type = false;
        }
    }
}

INSERTER(grammar::kwinterface)
{
    interface.documentation = pstate.global.moving_docstring;
    pstate.global.moving_docstring.clear();
}

INSERTER(grammar::kwerror)
{
    pstate.global.docstring = pstate.global.moving_docstring;
    pstate.global.moving_docstring.clear();
}

INSERTER(grammar::kwtype)
{
    pstate.global.docstring = pstate.global.moving_docstring;
    pstate.global.moving_docstring.clear();
}

INSERTER(grammar::kwmethod)
{
    pstate.global.docstring = pstate.global.moving_docstring;
    pstate.global.moving_docstring.clear();
}

INSERTER(grammar::kwarrow)
{
    auto& state = pstate.stack.back();
    pstate.global.method_params = state.last_element_type;
}

INSERTER(grammar::interface_name)
{
    interface.ifname = input.string();
    using StateT = typename grammar::parser_state<CallbackMap>::State;
    pstate.stack.emplace_back(StateT(input.position().byte));
}

INSERTER(grammar::error)
{
    for (const auto& m : interface.errors) {
        if (m.name == pstate.global.name)
            throw std::invalid_argument(
                "Multiple definition of error " + pstate.global.name);
    }
    auto& state = pstate.stack.back();
    interface.errors.emplace_back(
        pstate.global.name, pstate.global.docstring, state.last_element_type);
}

INSERTER(grammar::method)
{
    if (std::find_if(
            interface.methods.cbegin(),
            interface.methods.cend(),
            [name = pstate.global.name](auto& m) { return (m.name == name); })
        != interface.methods.cend()) {
        throw std::invalid_argument(
            "Multiple definition of method " + pstate.global.name);
    }
    auto& state = pstate.stack.back();
    using MethodCallback = decltype(CallbackMap::value_type::callback);
    MethodCallback callback{nullptr};
    const auto cbit = std::find_if(
        pstate.global.callbacks.begin(),
        pstate.global.callbacks.end(),
        [name = pstate.global.name](auto& p) { return p.method == name; });
    if (cbit != pstate.global.callbacks.end()) {
        callback = cbit->callback;
        pstate.global.callbacks.erase(cbit);
    }
    interface.methods.emplace_back(
        pstate.global.name,
        pstate.global.docstring,
        pstate.global.method_params,
        state.last_element_type,
        callback);
}

INSERTER(grammar::vtypedef)
{
    for (const auto& m : interface.types) {
        if (m.name == pstate.global.name)
            throw std::invalid_argument(
                "Multiple definition of type " + pstate.global.name);
    }
    auto& state = pstate.stack.back();
    interface.types.emplace_back(
        pstate.global.name, pstate.global.docstring, state.last_element_type);
}

#endif
