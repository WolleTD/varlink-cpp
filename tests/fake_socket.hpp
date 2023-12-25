#pragma once
#include <varlink/detail/config.hpp>

#if LIBVARLINK_USE_BOOST
#include <boost/asio/local/stream_protocol.hpp>
#else
#include <asio/local/stream_protocol.hpp>
#endif

using namespace varlink;

struct FakeSocket;

struct fake_proto {
    using socket = FakeSocket;
    using endpoint = net::local::stream_protocol::endpoint;
};

struct FakeSocket : net::socket_base {
    using protocol_type = fake_proto;
    using endpoint_type = protocol_type::endpoint;
    using executor_type = net::any_io_executor;

    explicit FakeSocket(net::io_context& ctx) : ctx_(&ctx) {}
    FakeSocket(net::io_context& ctx, net::local::stream_protocol, int) : ctx_(&ctx) {}
    FakeSocket(FakeSocket&& r) noexcept = default;
    FakeSocket& operator=(FakeSocket&& r) noexcept = default;

    ~FakeSocket()
    {
        if (validate) validate_write();
    }

    [[nodiscard]] executor_type get_executor() const { return ctx_->get_executor(); }

    // Fake control

    size_t setup_fake(const net::const_buffer& buffer)
    {
        return write_buffer(fake_data, buffer, true);
    }

    size_t setup_fake(const std::string& str)
    {
        return setup_fake(net::buffer(str.data(), str.size() + 1));
    }

    size_t expect(const net::const_buffer& buffer)
    {
        return write_buffer(sent_expect, buffer, true);
    }

    size_t expect(const std::string& str)
    {
        return expect(net::buffer(str.data(), str.size() + 1));
    }

    void validate_write() const
    {
        REQUIRE(
            std::string_view(sent_data.data(), sent_data.size())
            == std::string_view(sent_expect.data(), sent_expect.size()));
    }

    // Actual methods called by code

    size_t send(const net::const_buffer& buffer)
    {
        if (error_on_write) { throw std::system_error(net::error::broken_pipe); }
        else {
            return write_buffer(sent_data, buffer);
        }
    }

    template <typename CompletionHandler>
    auto async_write_some(const net::const_buffer& buffer, CompletionHandler&& handler)
    {
        net::post(
            ctx_->get_executor(),
            [this, buffer, handler = std::forward<CompletionHandler>(handler)]() mutable {
                if (not cancelled) {
                    if (error_on_write) { return handler(net::error::broken_pipe, 0); }
                    else {
                        auto n = send(buffer);
                        return handler(std::error_code{}, n);
                    }
                }
                else {
                    return handler(net::error::operation_aborted, 0);
                }
            });
    }

    size_t receive(const net::mutable_buffer& buffer)
    {
        if (fake_data.empty()) { throw std::system_error(net::error::eof); }
        else {
            const auto max_buffer = std::min(fake_data.size(), buffer.size());
            const auto read_count = std::min(write_max, max_buffer);
            std::memcpy(buffer.data(), fake_data.data(), read_count);
            fake_data.erase(
                fake_data.begin(), fake_data.begin() + static_cast<ptrdiff_t>(read_count));
            return read_count;
        }
    }

    template <typename CompletionHandler>
    auto async_receive(const net::mutable_buffer& buffer, CompletionHandler&& handler)
    {
        net::post(
            ctx_->get_executor(),
            [this, buffer, handler = std::forward<CompletionHandler>(handler)]() mutable {
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

    bool error_on_write{false};
    bool cancelled{false};
    bool validate{false};
    size_t write_max{BUFSIZ};

  private:
    size_t write_buffer(std::vector<char>& target, const net::const_buffer& buffer, bool all = false) const
    {
        const auto insert_pos = target.size();
        const auto write_count = all ? buffer.size() : std::min(write_max, buffer.size());
        target.resize(insert_pos + write_count);
        std::memcpy(&target[insert_pos], buffer.data(), write_count);
        return write_count;
    }

    std::vector<char> fake_data{};
    std::vector<char> sent_data{};
    std::vector<char> sent_expect{};
    net::io_context* ctx_;
};
