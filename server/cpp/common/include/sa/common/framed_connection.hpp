#pragma once

#include "sa/common/framing.hpp"
#include "sa/common/message_connection.hpp"
#include "sa/common/tcp_byte_stream.hpp"

#include <asio/ip/tcp.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace sa::common {

// Length-prefixed protobuf frames over a TcpByteStream (ADR-0004). Used by the native client
// link and by every internal link.
class FramedConnection final : public MessageConnection,
                               public std::enable_shared_from_this<FramedConnection> {
public:
    // `close_linger`: see TcpByteStream.
    FramedConnection(asio::ip::tcp::socket socket, FrameLimits limits,
                     std::optional<std::chrono::milliseconds> close_linger);

    void Start(Callbacks callbacks) override;
    [[nodiscard]] std::expected<void, SendError> Send(
        const google::protobuf::MessageLite& message) override;
    void Close() override { stream_->Close(); }
    void Abort() override { stream_->Abort(); }
    [[nodiscard]] bool is_open() const override { return stream_->is_open(); }
    [[nodiscard]] std::size_t QueuedBytes() const override { return stream_->QueuedBytes(); }
    [[nodiscard]] std::string PeerAddress() const override { return stream_->PeerAddress(); }
    [[nodiscard]] asio::any_io_executor executor() { return stream_->executor(); }

private:
    void OnBytes(std::span<const std::byte> bytes);

    std::shared_ptr<TcpByteStream> stream_;
    FrameLimits limits_;
    FrameDecoder decoder_;
    Callbacks callbacks_;
    std::string serialize_scratch_;
};

}  // namespace sa::common
