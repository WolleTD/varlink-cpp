#ifndef LIBVARLINK_TEST_ENV_WRAPPER_H
#define LIBVARLINK_TEST_ENV_WRAPPER_H

#include <memory>

class BaseEnvironment;

struct EnvironmentWrapper {
    ~EnvironmentWrapper();

    std::unique_ptr<BaseEnvironment> env_;
};

EnvironmentWrapper getWrappedEnvironment();
#endif // LIBVARLINK_TEST_ENV_WRAPPER_H
