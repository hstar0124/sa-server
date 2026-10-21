#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Real sockets for transport tests: a connected loopback pair whose server end goes to the code
// under test while the test plays the peer synchronously on the other end.
namespace sa::common::testing {

// Generous: the loopback round trips take microseconds, this only stops a broken test hanging.
inline constexpr std::chrono::seconds kRunLimit{5};
inline constexpr std::chrono::milliseconds kRunSlice{5};
inline constexpr std::size_t kPeerReadChunk = 1024;

// Runs `io` in slices until `done()` holds; a test that never gets there fails loudly.
inline void RunUntil(asio::io_context& io, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + kRunLimit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error{"test did not reach its condition in time"};
        }
        io.restart();
        io.run_for(kRunSlice);
    }
}

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

    // Peer reads keep the io_context running, so the side under test makes progress while the
    // peer waits: a completion handler may be what sends the bytes the peer is waiting for.
    [[nodiscard]] std::vector<std::byte> ReadExactlyAtPeer(std::size_t n) {
        std::vector<std::byte> out(n);
        std::size_t filled = 0;
        while (filled < n) {
            std::error_code ec;
            filled += ReadSomeAtPeer(std::span{out}.subspan(filled), ec);
            if (ec) {
                throw std::runtime_error{"peer read failed: " + ec.message()};
            }
        }
        return out;
    }

    struct PeerRead {
        std::vector<std::byte> bytes;
        std::error_code end;  // asio::error::eof for an orderly close, a reset otherwise
    };

    // Everything the peer receives until the other side closes or resets.
    [[nodiscard]] PeerRead ReadToEndAtPeer() {
        PeerRead read;
        std::array<std::byte, kPeerReadChunk> chunk{};
        while (!read.end) {
            const auto got = std::span{chunk}.first(ReadSomeAtPeer(chunk, read.end));
            read.bytes.insert(read.bytes.end(), got.begin(), got.end());
        }
        return read;
    }

    // An HTTP response head at the peer, up to and including the blank line. Reads byte by byte
    // so nothing after the head is consumed.
    [[nodiscard]] std::string ReadHttpHeadAtPeer() {
        std::string head;
        while (!head.ends_with("\r\n\r\n")) {
            head.push_back(static_cast<char>(ReadExactlyAtPeer(1).front()));
        }
        return head;
    }

    void RunUntil(const std::function<bool()>& done) { testing::RunUntil(io_, done); }

private:
    // One read at the peer of at most `into.size()` bytes, running the io_context in slices
    // while nothing has arrived.
    std::size_t ReadSomeAtPeer(std::span<std::byte> into, std::error_code& ec) {
        peer_.non_blocking(true);
        const auto deadline = std::chrono::steady_clock::now() + kRunLimit;
        while (true) {
            const std::size_t n = peer_.read_some(asio::buffer(into.data(), into.size()), ec);
            if (ec != asio::error::would_block) {
                return n;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                throw std::runtime_error{"loopback peer did not receive what it waited for"};
            }
            io_.restart();
            io_.run_for(kRunSlice);
        }
    }

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
