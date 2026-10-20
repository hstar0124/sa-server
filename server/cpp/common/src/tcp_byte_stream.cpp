#include "sa/common/tcp_byte_stream.hpp"

#include <asio/write.hpp>

#include <tuple>
#include <utility>

namespace sa::common {

TcpByteStream::TcpByteStream(asio::ip::tcp::socket socket,
                             std::optional<std::chrono::milliseconds> close_linger)
    : socket_(std::move(socket)),
      close_linger_(close_linger),
      linger_timer_(socket_.get_executor()) {}

void TcpByteStream::Start(Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    state_ = State::kOpen;
    ReadMore();
}

std::string TcpByteStream::PeerAddress() const {
    std::error_code ec;
    const auto endpoint = socket_.remote_endpoint(ec);
    return ec ? std::string{"?"} : endpoint.address().to_string();
}

void TcpByteStream::ReadMore() {
    socket_.async_read_some(asio::buffer(read_chunk_),
                            [self = shared_from_this()](const std::error_code& ec, std::size_t n) {
                                if (self->state_ == State::kClosed) {
                                    return;
                                }
                                if (ec) {
                                    // While closing, the end of the stream is the peer completing
                                    // the close.
                                    const bool completed =
                                        self->state_ == State::kClosing && ec == asio::error::eof;
                                    self->Finish(completed ? std::error_code{} : ec);
                                    return;
                                }
                                if (self->state_ == State::kOpen) {
                                    // A copy: the callback may close the stream, which drops
                                    // callbacks_ and with them the last reference to the connection
                                    // above, while the callback is still running.
                                    const auto on_bytes = self->callbacks_.on_bytes;
                                    on_bytes(std::span{self->read_chunk_.data(), n});
                                }  // while closing, inbound bytes are read only to be discarded
                                if (self->state_ != State::kClosed) {
                                    self->ReadMore();
                                }
                            });
}

void TcpByteStream::Write(std::vector<std::byte> bytes) {
    if (state_ != State::kOpen || bytes.empty()) {
        return;
    }
    const bool idle = write_queue_.empty();
    write_queue_.push_back(std::move(bytes));
    if (idle) {
        WriteNext();
    }
}

// NOLINTBEGIN(misc-no-recursion): the completion handler runs on a later io_context iteration.
void TcpByteStream::WriteNext() {
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
                              self->ShutdownSending();
                          }
                      });
}
// NOLINTEND(misc-no-recursion)

void TcpByteStream::Close() {
    if (state_ != State::kOpen) {
        return;
    }
    state_ = State::kClosing;
    if (close_linger_) {
        linger_timer_.expires_after(*close_linger_);
        linger_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
            if (!ec) {
                self->Abort();  // the peer neither took our bytes nor closed in time
            }
        });
    }
    if (write_queue_.empty()) {
        ShutdownSending();
    }
}

void TcpByteStream::ShutdownSending() {
    std::error_code ignored;
    std::ignore = socket_.shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
}

void TcpByteStream::Abort() {
    if (state_ == State::kClosed) {
        return;
    }
    write_queue_.clear();
    Finish(std::make_error_code(std::errc::connection_aborted));
}

void TcpByteStream::Finish(const std::error_code& reason) {
    if (state_ == State::kClosed) {
        return;
    }
    state_ = State::kClosed;
    linger_timer_.cancel();
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
