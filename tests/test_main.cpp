#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

class BaseEnvironment {};
extern std::unique_ptr<BaseEnvironment> getEnvironment();

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = getEnvironment();
    return Catch::Session().run(argc, argv);
}
