#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

class Environment {
  public:
    virtual ~Environment() = default;
};

extern std::unique_ptr<Environment> getEnvironment();

int main(int argc, char** argv)
{
    signal(SIGPIPE, SIG_IGN);
    auto env = getEnvironment();
    return Catch::Session().run(argc, argv);
}
