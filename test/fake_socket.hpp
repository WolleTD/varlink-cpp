#pragma once
#include <asio.hpp>

namespace net = ::asio;

class FakeSocket : public net::socket_base {
  public:
    using protocol_type = net::local::stream_protocol;
    using executor_type = net::any_io_executor;
    size_t write_max{BUFSIZ};

  private:
    std::vector<char> fake_data{};
    std::vector<char> sent_data{};
    std::vector<char> sent_expect{};
    net::io_context* ctx_;
    bool cancelled{false};

  public:
    explicit FakeSocket(net::io_context& ctx) : ctx_(&ctx) {}
    FakeSocket(
        [[maybe_unused]] net::io_context& ctx,
        [[maybe_unused]] net::local::stream_protocol p,
        [[maybe_unused]] int fd)
        : ctx_(&ctx)
    {
    }
    FakeSocket(FakeSocket&& r) noexcept = default;
    FakeSocket& operator=(FakeSocket&& r) noexcept = default;

    [[nodiscard]] executor_type get_executor() const
    {
        return ctx_->get_executor();
    }

    // Fake control

    size_t setup_fake(const net::const_buffer& buffer)
    {
        return write_buffer(fake_data, buffer);
    }

    size_t setup_fake(const std::string& str)
    {
        return setup_fake(net::buffer(str.data(), str.size() + 1));
    }

    size_t expect(const net::const_buffer& buffer)
    {
        return write_buffer(sent_expect, buffer);
    }

    size_t expect(const std::string& str)
    {
        return expect(net::buffer(str.data(), str.size() + 1));
    }

    void validate_write()
    {
        REQUIRE(
            std::string_view(sent_data.data(), sent_data.size())
            == std::string_view(sent_expect.data(), sent_expect.size()));
    }

    // Actual methods called by code

    size_t send(const net::const_buffer& buffer)
    {
        return write_buffer(sent_data, buffer);
    }

    template <typename CompletionHandler>
    auto async_write_some(const net::const_buffer& buffer, CompletionHandler&& handler)
    {
        net::post(
            ctx_->get_executor(),
            [this,
             buffer,
             handler = std::forward<CompletionHandler>(handler)]() mutable {
                if (not cancelled) {
                    auto n = send(buffer);
                    return handler(std::error_code{}, n);
                }
                else {
                    return handler(net::error::operation_aborted, 0);
                }
            });
    }

    size_t receive(const net::mutable_buffer& buffer)
    {
        if (fake_data.empty()) {
            throw std::system_error(net::error::eof);
        }
        else {
            const auto read_count = std::min(fake_data.size(), buffer.size());
            std::memcpy(buffer.data(), fake_data.data(), read_count);
            fake_data.erase(
                fake_data.begin(),
                fake_data.begin() + static_cast<ptrdiff_t>(read_count));
            return read_count;
        }
    }

    template <typename CompletionHandler>
    auto async_receive(const net::mutable_buffer& buffer, CompletionHandler&& handler)
    {
        net::post(
            ctx_->get_executor(),
            [this,
             buffer,
             handler = std::forward<CompletionHandler>(handler)]() mutable {
                try {
                    if (not cancelled) {
                        auto n = receive(buffer);
                        handler(std::error_code{}, n);
                    }
                    else {
                        handler(net::error::operation_aborted, 0);
                    }
                }
                catch (std::system_error& e) {
                    handler(e.code(), 0);
                }
            });
    }

    void cancel() { cancelled = true; }

  private:
    size_t write_buffer(std::vector<char>& target, const net::const_buffer& buffer) const
    {
        const auto insert_pos = target.size();
        const auto write_count = std::min(write_max, buffer.size());
        target.resize(insert_pos + write_count);
        std::memcpy(&target[insert_pos], buffer.data(), write_count);
        return write_count;
    }
};
