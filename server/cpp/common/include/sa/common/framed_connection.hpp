#pragma once

#include "sa/common/framing.hpp"

#include <asio/ip/tcp.hpp>
#include <google/protobuf/message_lite.h>

#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace sa::common {

// One TCP connection speaking length-prefixed protobuf frames (ADR-0004). Shared by every link in
// the system: client<->gateway, gateway<->zone, and the bot client. It owns the socket, the
// read loop, the frame decoder and the write queue; what the frames mean is the caller's job.
//
// All callbacks run on the socket's executor. After `on_closed` fires nothing else is invoked.
class FramedConnection : public std::enable_shared_from_this<FramedConnection> {
public:
    struct Callbacks {
        std::function<void(std::span<const std::byte>)> on_frame;
        std::function<void(const std::error_code&)> on_closed;  // error_code{} = orderly close
    };

    FramedConnection(asio::ip::tcp::socket socket, FrameLimits limits);

    void Start(Callbacks callbacks);

    // Queues one frame. Returns false (and sends nothing) if the message exceeds the frame limit.
    bool Send(const google::protobuf::MessageLite& message);

    // Flushes queued frames, then shuts the socket down. Idempotent.
    void Close();
    // Drops queued frames and shuts down now.
    void Abort();

    [[nodiscard]] bool is_open() const noexcept { return state_ == State::kOpen; }
    [[nodiscard]] std::string PeerAddress() const;
    [[nodiscard]] asio::any_io_executor executor() { return socket_.get_executor(); }

private:
    enum class State : std::uint8_t { kIdle, kOpen, kClosing, kClosed };
    static constexpr std::size_t kReadChunkBytes = std::size_t{16} * 1024;

    void ReadMore();
    void OnBytes(std::size_t count);
    void WriteNext();
    void Finish(const std::error_code& reason);

    asio::ip::tcp::socket socket_;
    FrameLimits limits_;
    FrameDecoder decoder_;
    Callbacks callbacks_;
    std::array<std::byte, kReadChunkBytes> read_chunk_{};
    std::deque<std::vector<std::byte>> write_queue_;
    std::string serialize_scratch_;
    State state_ = State::kIdle;
};

}  // namespace sa::common
