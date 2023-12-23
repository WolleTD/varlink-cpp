#define CATCH_CONFIG_RUNNER
#include "test_env_wrapper.h"
#include <csignal>
#include <catch2/catch_session.hpp>

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = getWrappedEnvironment();
    return Catch::Session().run(argc, argv);
}
