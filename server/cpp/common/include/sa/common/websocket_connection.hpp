#pragma once

#include "sa/common/framing.hpp"
#include "sa/common/message_connection.hpp"
#include "sa/common/tcp_byte_stream.hpp"
#include "sa/common/websocket.hpp"

#include <asio/ip/tcp.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace sa::common {

// Protobuf messages as WebSocket binary frames (ADR-0004: the browser viewer's transport). Runs
// the opening handshake itself, answers pings, honours close, and refuses text or fragmented
// frames because the game protocol never uses them.
class WebSocketConnection final : public MessageConnection,
                                  public std::enable_shared_from_this<WebSocketConnection> {
public:
    WebSocketConnection(asio::ip::tcp::socket socket, FrameLimits limits);

    void Start(Callbacks callbacks) override;
    bool Send(const google::protobuf::MessageLite& message) override;
    void Close() override;
    void Abort() override { stream_->Abort(); }
    [[nodiscard]] bool is_open() const override {
        return state_ == State::kOpen && stream_->is_open();
    }
    [[nodiscard]] std::string PeerAddress() const override { return stream_->PeerAddress(); }

private:
    enum class State : std::uint8_t { kHandshaking, kOpen, kClosing, kClosed };

    void OnBytes(std::span<const std::byte> bytes);
    bool TryCompleteHandshake();
    void DrainFrames();
    void OnFrame(websocket::Frame frame);
    void WriteText(std::string_view text);
    void FailProtocol(websocket::CloseStatus status);
    void WriteFrame(websocket::Opcode opcode, std::span<const std::byte> payload);

    std::shared_ptr<TcpByteStream> stream_;
    FrameLimits limits_;
    websocket::FrameDecoder decoder_;
    Callbacks callbacks_;
    std::string handshake_buffer_;
    std::string serialize_scratch_;
    State state_ = State::kHandshaking;
};

}  // namespace sa::common
