#include "sa/common/framed_connection.hpp"

#include <asio/write.hpp>

#include <tuple>
#include <utility>

namespace sa::common {

FramedConnection::FramedConnection(asio::ip::tcp::socket socket, FrameLimits limits)
    : socket_(std::move(socket)), limits_(limits), decoder_(limits) {}

void FramedConnection::Start(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    state_ = State::kOpen;
    ReadMore();
}

std::string FramedConnection::PeerAddress() const {
    std::error_code ec;
    const auto endpoint = socket_.remote_endpoint(ec);
    return ec ? std::string{"?"} : endpoint.address().to_string();
}

void FramedConnection::ReadMore() {
    socket_.async_read_some(asio::buffer(read_chunk_),
                            [self = shared_from_this()](const std::error_code& ec, std::size_t n) {
                                if (self->state_ != State::kOpen) {
                                    return;
                                }
                                if (ec) {
                                    self->Finish(ec);
                                    return;
                                }
                                self->OnBytes(n);
                            });
}

void FramedConnection::OnBytes(std::size_t count) {
    decoder_.Feed(std::span{read_chunk_.data(), count});
    while (state_ == State::kOpen) {
        auto popped = decoder_.TryPop();
        if (!popped) {
            Finish(std::make_error_code(std::errc::bad_message));
            return;
        }
        if (!*popped) {
            break;
        }
        callbacks_.on_frame(**popped);
    }
    if (state_ == State::kOpen) {
        ReadMore();
    }
}

bool FramedConnection::Send(const google::protobuf::MessageLite& message) {
    if (state_ != State::kOpen) {
        return false;
    }
    if (!message.SerializeToString(&serialize_scratch_)) {
        return false;
    }
    std::vector<std::byte> frame;
    if (!EncodeFrame(std::as_bytes(std::span{serialize_scratch_}), limits_, frame)) {
        return false;
    }
    const bool idle = write_queue_.empty();
    write_queue_.push_back(std::move(frame));
    if (idle) {
        WriteNext();
    }
    return true;
}

// NOLINTBEGIN(misc-no-recursion): the completion handler runs on a later io_context iteration.
void FramedConnection::WriteNext() {
    asio::async_write(socket_, asio::buffer(write_queue_.front()),
                      [self = shared_from_this()](const std::error_code& ec, std::size_t) {
                          if (self->state_ == State::kClosed) {
                              return;
                          }
                          if (ec) {
                              self->Finish(ec);
                              return;
                          }
                          self->write_queue_.pop_front();
                          if (!self->write_queue_.empty()) {
                              self->WriteNext();
                          } else if (self->state_ == State::kClosing) {
                              self->Finish({});
                          }
                      });
}
// NOLINTEND(misc-no-recursion)

void FramedConnection::Close() {
    if (state_ != State::kOpen) {
        return;
    }
    state_ = State::kClosing;
    if (write_queue_.empty()) {
        Finish({});
    }
}

void FramedConnection::Abort() {
    if (state_ == State::kClosed) {
        return;
    }
    write_queue_.clear();
    Finish(std::make_error_code(std::errc::connection_aborted));
}

void FramedConnection::Finish(const std::error_code& reason) {
    if (state_ == State::kClosed) {
        return;
    }
    state_ = State::kClosed;
    std::error_code ignored;
    std::ignore = socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    std::ignore = socket_.close(ignored);
    if (callbacks_.on_closed) {
        auto on_closed = std::move(callbacks_.on_closed);
        callbacks_ = {};
        on_closed(reason);
    }
}

}  // namespace sa::common
