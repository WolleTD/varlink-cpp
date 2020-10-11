# C++17 implementation of the Varlink protocol

This is an implementation of [Varlink](https://varlink.org) in C++17. It's not yet complete,
but as TCP and Unix connections are working and certification succeeds, I thought to share it.

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

Server:

```cpp
#include <varlink/server.hpp>
#include "org.example.more.varlink.hpp"

...

{
// server starts listening immediately
auto threaded_server = varlink::threaded_server("unix:/tmp/example.varlink",
                {"vendor", "product", "version", "url"});

// interfaces can only be added once and callbacks can't be changed
threaded_server.add_interface(org_example_more_varlink, varlink::callback_map{
    // varlink_callback is a macro containing the callback parameter list and
    // a trailing return value. It can also be used for named functions
    {"Ping", [] varlink_callback { return {{"pong", parameters["ping"]}}; }}
});

// optional, if needed
// threaded_server.join();
} // dtor will close socket and join threads
```

Client:

```cpp
#include <varlink/client.hpp>

...

{
// ctor will connect successfully or throw
auto client = varlink::varlink_client{"unix:/tmp/example.varlink"};

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
