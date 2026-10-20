#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

// Real sockets for transport tests: a connected loopback pair whose server end goes to the code
// under test while the test plays the peer synchronously on the other end.
namespace sa::common::testing {

// Generous: the loopback round trips take microseconds, this only stops a broken test hanging.
inline constexpr std::chrono::seconds kRunLimit{5};
inline constexpr std::chrono::milliseconds kRunSlice{5};

class Loopback {
public:
    Loopback() {
        asio::ip::tcp::acceptor acceptor{io_, {asio::ip::make_address("127.0.0.1"), 0}};
        peer_.connect(acceptor.local_endpoint());
        acceptor.accept(server_);
    }

    [[nodiscard]] asio::io_context& io() noexcept { return io_; }
    [[nodiscard]] asio::ip::tcp::socket TakeServerSocket() { return std::move(server_); }
    [[nodiscard]] asio::ip::tcp::socket& peer() noexcept { return peer_; }

    void WriteFromPeer(std::span<const std::byte> bytes) {
        asio::write(peer_, asio::buffer(bytes));
    }
    void WriteFromPeer(std::string_view text) { asio::write(peer_, asio::buffer(text)); }

    // Runs the io_context until `done()` holds; a test that never gets there fails loudly.
    void RunUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + kRunLimit;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                throw std::runtime_error{"loopback test did not reach its condition in time"};
            }
            io_.restart();
            io_.run_for(kRunSlice);
        }
    }

private:
    asio::io_context io_;
    asio::ip::tcp::socket peer_{io_};
    asio::ip::tcp::socket server_{io_};
};

// What a browser sends to open a WebSocket (RFC 6455 §1.3 example key).
inline constexpr std::string_view kWebSocketHandshake =
    "GET /game HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

inline constexpr std::byte kFin{0x80};
inline constexpr std::byte kMasked{0x80};
inline constexpr std::size_t kMaskBytes = 4;
inline constexpr std::size_t kMaxShortPayload = 125;

// A client frame with a zero mask (masking with zeros leaves the payload as is). Short payloads
// only, which is all the tests need.
inline void AppendClientFrame(std::vector<std::byte>& out, std::byte opcode, bool fin,
                              std::span<const std::byte> payload) {
    if (payload.size() > kMaxShortPayload) {
        throw std::logic_error{"test frames carry at most 125 bytes"};
    }
    out.push_back(fin ? (kFin | opcode) : opcode);
    out.push_back(kMasked | static_cast<std::byte>(payload.size()));
    out.insert(out.end(), kMaskBytes, std::byte{0});
    out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace sa::common::testing
