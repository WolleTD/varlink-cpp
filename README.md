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
    // varlink_more_callback is a macro containing the callback parameter list
    {"Ping", [] varlink_more_callback { send_reply({{"pong", parameters["ping"]}}, /* continues = */ false); }}
});

varlink_srv.async_serve_forever();
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

// .call returns a json object of the result ["parameters"]
auto reply = client.call("org.example.more.Ping", R"({"ping":"Test"})"_json);
// all parameters are validated by the server
assert(reply["pong"].get<std::string>() == "Test");

// .call_more returns a callable object
auto more = client.call_more("org.example.more.TestMore", json{{"n","1"}});
reply = more();
}
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
                  [&](auto ec, const json& rep){
    reply = rep;
});

ctx.run();
assert(reply["pong"].get<std::string>() == "Test");
}
```
