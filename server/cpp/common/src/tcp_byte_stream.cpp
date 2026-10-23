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
    queued_bytes_ += bytes.size();
    pending_.push_back(std::move(bytes));
    if (in_flight_.empty()) {
        WriteGathered();
    }
}

// Moves everything pending into one scatter-gather write. The completion handler runs on a later
// io_context iteration, so the "recursion" is a loop over the socket's lifetime.
// NOLINTBEGIN(misc-no-recursion)
void TcpByteStream::WriteGathered() {
    in_flight_ = std::exchange(pending_, {});
    gather_.clear();
    gather_.reserve(in_flight_.size());
    for (const std::vector<std::byte>& bytes : in_flight_) {
        gather_.emplace_back(asio::buffer(bytes));
    }
    asio::async_write(socket_, gather_,
                      [self = shared_from_this()](const std::error_code& ec, std::size_t written) {
                          if (self->state_ == State::kClosed) {
                              return;
                          }
                          if (ec) {
                              self->Finish(ec);
                              return;
                          }
                          self->queued_bytes_ -= written;
                          self->in_flight_.clear();
                          if (!self->pending_.empty()) {
                              self->WriteGathered();
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
    if (in_flight_.empty()) {
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
    pending_.clear();
    Finish(std::make_error_code(std::errc::connection_aborted));
}

void TcpByteStream::Finish(const std::error_code& reason) {
    if (state_ == State::kClosed) {
        return;
    }
    state_ = State::kClosed;
    queued_bytes_ = 0;
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
