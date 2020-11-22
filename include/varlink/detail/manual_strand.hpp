// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// originally from https://github.com/qchateau/packio

#ifndef LIBVARLINK_MANUAL_STRAND_H
#define LIBVARLINK_MANUAL_STRAND_H

#include <queue>
#include <asio/strand.hpp>
#include <varlink/detail/config.hpp>
#include <varlink/detail/unique_function.hpp>

namespace varlink::detail {
template <typename Executor>
class manual_strand {
  public:
    using function_type = unique_function<void()>;

    explicit manual_strand(const Executor& executor) : strand_{executor} {}

    void push(function_type function)
    {
        net::dispatch(strand_, [this, function = std::move(function)]() mutable {
            queue_.push(std::move(function));

            if (not executing_) {
                executing_ = true;
                execute();
            }
        });
    }

    void next()
    {
        net::dispatch(strand_, [this] { execute(); });
    }

  private:
    void execute()
    {
        if (queue_.empty()) {
            executing_ = false;
            return;
        }

        auto function = std::move(queue_.front());
        queue_.pop();
        function();
    }

    net::strand<Executor> strand_;
    std::queue<function_type> queue_;
    bool executing_{false};
};
} // namespace varlink::detail

#endif // LIBVARLINK_JOB_QUEUE_HPP
