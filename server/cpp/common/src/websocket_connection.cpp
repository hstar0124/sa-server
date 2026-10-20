#include "sa/common/websocket_connection.hpp"

#include "sa/common/bytes.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace sa::common {

using websocket::Opcode;

WebSocketConnection::WebSocketConnection(asio::ip::tcp::socket socket, FrameLimits limits,
                                         std::optional<std::chrono::milliseconds> close_linger)
    : stream_(std::make_shared<TcpByteStream>(std::move(socket), close_linger)),
      limits_(limits),
      decoder_(limits.max_payload_bytes) {}

void WebSocketConnection::Start(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    stream_->Start(TcpByteStream::Callbacks{
        .on_bytes = [self = shared_from_this()](std::span<const std::byte> b) { self->OnBytes(b); },
        .on_closed =
            [self = shared_from_this()](const std::error_code& ec) {
                self->state_ = State::kClosed;
                if (self->callbacks_.on_closed) {
                    auto on_closed = std::move(self->callbacks_.on_closed);
                    self->callbacks_ = {};
                    on_closed(ec);
                }
            },
    });
}

void WebSocketConnection::OnBytes(std::span<const std::byte> bytes) {
    if (state_ == State::kHandshaking) {
        handshake_buffer_ += ToWireString(bytes);
        if (!TryCompleteHandshake()) {
            return;  // still reading the request, or it was rejected
        }
        // Bytes after the header terminator belong to the first frame.
        decoder_.Feed(AsBytes(handshake_buffer_));
        handshake_buffer_.clear();
    } else {
        decoder_.Feed(bytes);
    }
    DrainFrames();
}

// Parses the buffered request. On success the buffer holds only bytes after the request.
bool WebSocketConnection::TryCompleteHandshake() {
    const auto request = websocket::ParseHandshake(handshake_buffer_);
    if (!request) {
        if (request.error() != websocket::HandshakeError::kIncomplete) {
            spdlog::debug("websocket {}: handshake rejected ({})", PeerAddress(),
                          static_cast<int>(request.error()));
            WriteText(websocket::RejectionResponse());
            stream_->Close();
        }
        return false;
    }
    WriteText(websocket::HandshakeResponse(websocket::AcceptKey(request->key)));
    state_ = State::kOpen;
    const std::size_t header_end = handshake_buffer_.find("\r\n\r\n") + 4;
    handshake_buffer_.erase(0, header_end);
    return true;
}

void WebSocketConnection::DrainFrames() {
    while (state_ == State::kOpen) {
        auto popped = decoder_.TryPop();
        if (!popped) {
            FailProtocol(popped.error() == websocket::FrameError::kPayloadTooLarge
                             ? websocket::CloseStatus::kMessageTooBig
                             : websocket::CloseStatus::kProtocolError);
            return;
        }
        if (!*popped) {
            return;
        }
        OnFrame(std::move(**popped));
    }
}

void WebSocketConnection::WriteText(std::string_view text) {
    const auto bytes = AsBytes(text);
    stream_->Write(std::vector<std::byte>(bytes.begin(), bytes.end()));
}

void WebSocketConnection::OnFrame(websocket::Frame frame) {
    switch (frame.opcode) {
        case Opcode::kBinary:
            if (!frame.fin) {
                FailProtocol(
                    websocket::CloseStatus::kUnsupportedData);  // fragmentation is not used
                return;
            }
            {
                // A copy: the handler may close this connection, which clears callbacks_.
                const auto on_message = callbacks_.on_message;
                on_message(frame.payload);
            }
            return;
        case Opcode::kPing:
            WriteFrame(Opcode::kPong, frame.payload);
            return;
        case Opcode::kPong:
            return;
        case Opcode::kClose:
            // Echo the close and finish once it is flushed (RFC 6455 §5.5.1).
            state_ = State::kClosing;
            WriteFrame(Opcode::kClose, frame.payload);
            stream_->Close();
            return;
        case Opcode::kText:
        case Opcode::kContinuation:
            FailProtocol(websocket::CloseStatus::kUnsupportedData);
            return;
    }
}

void WebSocketConnection::FailProtocol(websocket::CloseStatus status) {
    if (state_ != State::kOpen) {
        return;
    }
    state_ = State::kClosing;
    const auto payload = websocket::ClosePayload(status);
    WriteFrame(Opcode::kClose, payload);
    stream_->Close();
}

void WebSocketConnection::WriteFrame(Opcode opcode, std::span<const std::byte> payload) {
    std::vector<std::byte> frame;
    websocket::EncodeFrame(opcode, payload, frame);
    stream_->Write(std::move(frame));
}

bool WebSocketConnection::Send(const google::protobuf::MessageLite& message) {
    if (state_ != State::kOpen || !message.SerializeToString(&serialize_scratch_)) {
        return false;
    }
    if (serialize_scratch_.size() > limits_.max_payload_bytes) {
        return false;
    }
    WriteFrame(Opcode::kBinary, AsBytes(serialize_scratch_));
    return true;
}

void WebSocketConnection::Close() {
    if (state_ == State::kOpen) {
        state_ = State::kClosing;
        const auto payload = websocket::ClosePayload(websocket::CloseStatus::kNormal);
        WriteFrame(Opcode::kClose, payload);
    }
    stream_->Close();
}

}  // namespace sa::common
