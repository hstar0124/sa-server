#include "sa/common/websocket.hpp"

#include "sa/common/bytes.hpp"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>

namespace sa::common::websocket {
namespace {

constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::string_view kVersion = "13";  // the only version RFC 6455 defines
constexpr std::size_t kMaxControlPayload = 125;
constexpr std::size_t kMaskBytes = 4;
constexpr std::uint8_t kFinBit = 0x80;
constexpr std::uint8_t kRsvMask = 0x70;
constexpr std::uint8_t kOpcodeMask = 0x0F;
constexpr std::uint8_t kMaskBit = 0x80;
constexpr std::uint8_t kLenMask = 0x7F;
constexpr std::uint8_t kLen16 = 126;
constexpr std::uint8_t kLen64 = 127;

std::string ToLower(std::string_view s) {
    std::string out{s};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

// Header values, lower-cased names; multiple values joined with ", " per RFC 7230.
struct Headers {
    std::vector<std::pair<std::string, std::string>> fields;

    [[nodiscard]] std::optional<std::string_view> Get(std::string_view name) const {
        for (const auto& [n, v] : fields) {
            if (n == name) {
                return v;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] bool TokenIn(std::string_view name, std::string_view token) const {
        const auto value = Get(name);
        if (!value) {
            return false;
        }
        const std::string lower = ToLower(*value);
        std::string_view rest{lower};
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const std::string_view item = Trim(rest.substr(0, comma));
            if (item == token) {
                return true;
            }
            if (comma == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(comma + 1);
        }
        return false;
    }
};

}  // namespace

// ---- handshake ----------------------------------------------------------------------------------

std::expected<HandshakeRequest, HandshakeError> ParseHandshake(std::string_view received) {
    const auto end = received.find(kHandshakeEnd);
    if (end == std::string_view::npos) {
        return std::unexpected(received.size() >= kMaxHandshakeBytes ? HandshakeError::kTooLarge
                                                                     : HandshakeError::kIncomplete);
    }
    const std::string_view head = received.substr(0, end);

    // Request line: GET <path> HTTP/1.1
    const auto line_end = head.find("\r\n");
    const std::string_view request_line = head.substr(0, line_end);
    const auto sp1 = request_line.find(' ');
    const auto sp2 = request_line.rfind(' ');
    if (sp1 == std::string_view::npos || sp2 == sp1 || request_line.substr(0, sp1) != "GET" ||
        request_line.substr(sp2 + 1) != "HTTP/1.1") {
        return std::unexpected(HandshakeError::kNotAnUpgrade);
    }
    HandshakeRequest request;
    request.path = std::string{request_line.substr(sp1 + 1, sp2 - sp1 - 1)};

    Headers headers;
    std::string_view rest =
        line_end == std::string_view::npos ? std::string_view{} : head.substr(line_end + 2);
    while (!rest.empty()) {
        const auto eol = rest.find("\r\n");
        const std::string_view line = rest.substr(0, eol);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            headers.fields.emplace_back(ToLower(Trim(line.substr(0, colon))),
                                        std::string{Trim(line.substr(colon + 1))});
        }
        if (eol == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(eol + 2);
    }

    if (!headers.TokenIn("upgrade", "websocket") || !headers.TokenIn("connection", "upgrade")) {
        return std::unexpected(HandshakeError::kNotAnUpgrade);
    }
    if (headers.Get("sec-websocket-version").value_or("") != kVersion) {
        return std::unexpected(HandshakeError::kBadVersion);
    }
    const auto key = headers.Get("sec-websocket-key");
    if (!key || key->empty()) {
        return std::unexpected(HandshakeError::kMissingKey);
    }
    request.key = std::string{*key};
    return request;
}

std::string AcceptKey(std::string_view client_key) {
    std::string material{client_key};
    material += kGuid;
    std::array<std::byte, SHA_DIGEST_LENGTH> digest{};
    // OpenSSL takes unsigned char*; this is the only bridge from std::byte in this file.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    SHA1(reinterpret_cast<const unsigned char*>(material.data()), material.size(),
         reinterpret_cast<unsigned char*>(digest.data()));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    return EncodeBase64(digest);
}

std::string HandshakeResponse(std::string_view accept_key) {
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    response += accept_key;
    response += "\r\n\r\n";
    return response;
}

std::string RejectionResponse() {
    return "HTTP/1.1 400 Bad Request\r\n"
           "Connection: close\r\n"
           "Content-Length: 0\r\n\r\n";
}

// ---- frames -------------------------------------------------------------------------------------

void FrameDecoder::Feed(std::span<const std::byte> bytes) {
    CompactIfWorthwhile();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void FrameDecoder::CompactIfWorthwhile() {
    if (read_pos_ > 0 && (read_pos_ == buffer_.size() || read_pos_ > buffer_.size() / 2)) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
        read_pos_ = 0;
    }
}

std::expected<std::optional<Frame>, FrameError> FrameDecoder::TryPop() {
    const std::span<const std::byte> unread = std::span{buffer_}.subspan(read_pos_);
    if (unread.size() < 2) {
        return std::nullopt;
    }
    const auto b0 = std::to_integer<std::uint8_t>(unread[0]);
    const auto b1 = std::to_integer<std::uint8_t>(unread[1]);
    if ((b0 & kRsvMask) != 0) {
        return std::unexpected(FrameError::kReservedBits);
    }
    const auto opcode_raw = static_cast<std::uint8_t>(b0 & kOpcodeMask);
    const bool known = opcode_raw <= static_cast<std::uint8_t>(Opcode::kBinary) ||
                       (opcode_raw >= static_cast<std::uint8_t>(Opcode::kClose) &&
                        opcode_raw <= static_cast<std::uint8_t>(Opcode::kPong));
    if (!known) {
        return std::unexpected(FrameError::kUnknownOpcode);
    }
    if ((b1 & kMaskBit) == 0) {
        return std::unexpected(FrameError::kUnmaskedClientFrame);
    }

    std::size_t header = 2;
    std::uint64_t length = b1 & kLenMask;
    if (length == kLen16) {
        if (unread.size() < header + 2) {
            return std::nullopt;
        }
        std::uint16_t len16 = 0;
        std::memcpy(&len16, unread.subspan(header, 2).data(), 2);
        length = std::byteswap(len16);
        header += 2;
    } else if (length == kLen64) {
        if (unread.size() < header + 8) {
            return std::nullopt;
        }
        std::uint64_t len64 = 0;
        std::memcpy(&len64, unread.subspan(header, 8).data(), 8);
        length = std::byteswap(len64);
        header += 8;
    }
    const auto opcode = static_cast<Opcode>(opcode_raw);
    const bool control = opcode_raw >= static_cast<std::uint8_t>(Opcode::kClose);
    if (control && length > kMaxControlPayload) {
        return std::unexpected(FrameError::kControlFrameTooLong);
    }
    if (control && (b0 & kFinBit) == 0) {
        return std::unexpected(FrameError::kFragmentedControlFrame);
    }
    if (length > max_payload_bytes_) {
        return std::unexpected(FrameError::kPayloadTooLarge);
    }
    const std::size_t total = header + kMaskBytes + static_cast<std::size_t>(length);
    if (unread.size() < total) {
        return std::nullopt;
    }

    const std::span<const std::byte, kMaskBytes> mask = unread.subspan(header).first<kMaskBytes>();
    const std::span<const std::byte> masked =
        unread.subspan(header + kMaskBytes, static_cast<std::size_t>(length));
    Frame frame{.opcode = opcode, .fin = (b0 & kFinBit) != 0};
    frame.payload.resize(masked.size());
    for (std::size_t i = 0; i < masked.size(); ++i) {
        frame.payload[i] = masked[i] ^ mask[i % kMaskBytes];
    }
    read_pos_ += total;
    return frame;
}

void EncodeFrame(Opcode opcode, std::span<const std::byte> payload, std::vector<std::byte>& out) {
    out.push_back(
        std::byte{static_cast<std::uint8_t>(kFinBit | static_cast<std::uint8_t>(opcode))});
    if (payload.size() < kLen16) {
        out.push_back(std::byte{static_cast<std::uint8_t>(payload.size())});
    } else if (payload.size() <= UINT16_MAX) {
        out.push_back(std::byte{kLen16});
        const std::uint16_t be = std::byteswap(static_cast<std::uint16_t>(payload.size()));
        std::array<std::byte, 2> len{};
        std::memcpy(len.data(), &be, 2);
        out.insert(out.end(), len.begin(), len.end());
    } else {
        out.push_back(std::byte{kLen64});
        const std::uint64_t be = std::byteswap(std::uint64_t{payload.size()});
        std::array<std::byte, 8> len{};
        std::memcpy(len.data(), &be, 8);
        out.insert(out.end(), len.begin(), len.end());
    }
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<std::byte> ClosePayload(CloseStatus status) {
    const std::uint16_t be = std::byteswap(static_cast<std::uint16_t>(status));
    std::vector<std::byte> out(2);
    std::memcpy(out.data(), &be, 2);
    return out;
}

}  // namespace sa::common::websocket
