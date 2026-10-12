#include "sa/common/framed_connection.hpp"

#include <utility>

namespace sa::common {

FramedConnection::FramedConnection(asio::ip::tcp::socket socket, FrameLimits limits)
    : stream_(std::make_shared<TcpByteStream>(std::move(socket))),
      limits_(limits),
      decoder_(limits) {}

void FramedConnection::Start(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    stream_->Start(TcpByteStream::Callbacks{
        .on_bytes = [self = shared_from_this()](std::span<const std::byte> b) { self->OnBytes(b); },
        .on_closed =
            [self = shared_from_this()](const std::error_code& ec) {
                if (self->callbacks_.on_closed) {
                    auto on_closed = std::move(self->callbacks_.on_closed);
                    self->callbacks_ = {};
                    on_closed(ec);
                }
            },
    });
}

void FramedConnection::OnBytes(std::span<const std::byte> bytes) {
    decoder_.Feed(bytes);
    while (stream_->is_open()) {
        auto popped = decoder_.TryPop();
        if (!popped) {
            stream_->Abort();  // the stream cannot resynchronise after a bad length
            return;
        }
        if (!*popped) {
            return;
        }
        callbacks_.on_message(**popped);
    }
}

bool FramedConnection::Send(const google::protobuf::MessageLite& message) {
    if (!stream_->is_open() || !message.SerializeToString(&serialize_scratch_)) {
        return false;
    }
    std::vector<std::byte> frame;
    if (!EncodeFrame(std::as_bytes(std::span{serialize_scratch_}), limits_, frame)) {
        return false;
    }
    stream_->Write(std::move(frame));
    return true;
}

}  // namespace sa::common
