#define CATCH_CONFIG_RUNNER
#include "test_env.hpp"
#include <catch2/catch.hpp>

extern std::unique_ptr<BaseEnvironment> getEnvironment();

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = getEnvironment();
    return Catch::Session().run(argc, argv);
}
