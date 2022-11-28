#define CATCH_CONFIG_RUNNER
#include "test_env_wrapper.h"
#include <catch2/catch.hpp>

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = getWrappedEnvironment();
    return Catch::Session().run(argc, argv);
}
