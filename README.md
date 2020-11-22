# C++17 implementation of the Varlink protocol

This is an implementation of [Varlink](https://varlink.org) in C++17 built on top of asio.
It's not yet feature complete, but in general it's working.

It is using [nlohmann/json](https://github.com/nlohmann/json) as data format and provides a
`std::function` service callback interface. I consider it work in progress still, API may change.

## Basic usage

```cmake
include(VarlinkWrapper)
# Generate C++ header
varlink_wrapper(org.example.more.varlink)

# Add to taret
add_executable(example [...] org.example.more.varlink.hpp)
```

## Server

```cpp
#include <varlink/server.hpp>
#include "org.example.more.varlink.hpp"

...

{
// varlink_server class provides encapsulation for an async_server and a varlink_service
// and resolves varlink-uris to the corresponding asio endpoints
varlink::net::io_context ctx{};
auto varlink_srv = varlink::varlink_server(ctx, "unix:/tmp/example.varlink",
                {"vendor", "product", "version", "url"});

// interfaces can only be added once and callbacks can't be changed
varlink_srv.add_interface(org_example_more_varlink, varlink::callback_map{
    // varlink_callback is a macro containing the callback parameter list
    {"Ping", [] varlink_callback { send_reply({{"pong", parameters["ping"]}}, /* continues = */ false); }}
});

ctx.run();
}
```

## Client (sync):

```cpp
#include <varlink/client.hpp>

...

{
// Connect on creation
varlink::net::io_context ctx{};
auto client = varlink::varlink_client{ctx, "unix:/tmp/example.varlink"};

// .call returns a callable object which will retrieve the result
auto read_reply = client.call("org.example.more.Ping", R"({"ping":"Test"})"_json);

// either returns parameters only or throws varlink::varlink_error on error
auto reply = read_reply();

// parameter contents and types of the reply are validated by the service as well
assert(reply["pong"].get<std::string>() == "Test");

// subsequent calls to read_reply() will return more replies if a "more" call continues
// or nullptr if no more replies are available (immediately on "oneway" calls)
assert(read_reply() == nullptr);
} // dtor closes connection
```

## Client (async):

```cpp
#include <varlink/client.hpp>

...

{
// Connect on creation
varlink::net::io_context ctx{};
auto client = varlink::varlink_client{ctx, "unix:/tmp/example.varlink"};

json reply{};
// async_call (currently) captures the message by value
client.async_call(varlink::varlink_message("org.example.more.Ping", R"({"ping":"Test"})"_json),
                  [](auto ec, const json& rep, bool continues){
    reply = rep;
});

ctx.run();
assert(reply["pong"].get<std::string>() == "Test");
}
```
