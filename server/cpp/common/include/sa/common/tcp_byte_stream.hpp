#pragma once

#include <asio/ip/tcp.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace sa::common {

// One TCP socket as a stream of bytes: a read loop, an ordered write queue, and a single
// `on_closed` notification. Framing lives above it (FramedConnection, WebSocketConnection), so
// every transport shares one implementation of the parts that are easy to get wrong.
//
// All callbacks run on the socket's executor. After `on_closed` fires nothing else is invoked.
class TcpByteStream : public std::enable_shared_from_this<TcpByteStream> {
public:
    struct Callbacks {
        std::function<void(std::span<const std::byte>)> on_bytes;
        std::function<void(const std::error_code&)> on_closed;  // error_code{} = orderly close
    };

    explicit TcpByteStream(asio::ip::tcp::socket socket);

    void Start(Callbacks callbacks);

    // Queues bytes; writes are issued in order. Ignored once closing.
    void Write(std::vector<std::byte> bytes);
    // Flushes queued writes, then shuts the socket down. Idempotent.
    void Close();
    // Drops queued writes and shuts down now.
    void Abort();

    [[nodiscard]] bool is_open() const noexcept { return state_ == State::kOpen; }
    [[nodiscard]] std::string PeerAddress() const;
    [[nodiscard]] asio::any_io_executor executor() { return socket_.get_executor(); }

private:
    enum class State : std::uint8_t { kIdle, kOpen, kClosing, kClosed };
    static constexpr std::size_t kReadChunkBytes = std::size_t{16} * 1024;

    void ReadMore();
    void WriteNext();
    void Finish(const std::error_code& reason);

    asio::ip::tcp::socket socket_;
    Callbacks callbacks_;
    std::array<std::byte, kReadChunkBytes> read_chunk_{};
    std::deque<std::vector<std::byte>> write_queue_;
    State state_ = State::kIdle;
};

}  // namespace sa::common
