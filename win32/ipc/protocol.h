#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcitx::win32::ipc {

constexpr uint32_t kMagic = 0x46574358;
constexpr uint16_t kVersion = 3;
constexpr uint32_t kMaxPayloadSize = 1024 * 1024;
constexpr size_t kHeaderSize = 28;

enum class MessageType : uint16_t {
    Hello = 1,
    HelloReply = 2,
    CreateContext = 3,
    CreateContextReply = 4,
    FocusIn = 5,
    FocusOut = 6,
    DestroyContext = 7,
    KeyDown = 8,
    KeyUp = 9,
    KeyReply = 10,
    Error = 11,
    Ack = 12,
    Reset = 13,
    PollState = 14,
    SetMode = 15,
};

constexpr uint32_t kMaxCandidates = 32;

struct Candidate {
    std::string text;
    std::string label;
    std::string comment;
};

struct KeyReply {
    bool consumed = false;
    bool enabled = true;
    std::string commit;
    std::string preedit;
    uint32_t preeditCursor = 0;
    uint64_t revision = 0;
    uint32_t selected = UINT32_MAX;
    bool hasPrev = false;
    bool hasNext = false;
    std::vector<Candidate> candidates;
};

struct Frame {
    MessageType type = MessageType::Error;
    uint64_t requestId = 0;
    uint64_t contextId = 0;
    std::vector<uint8_t> payload;
};

class Writer {
  public:
    void u8(uint8_t value) { data_.push_back(value); }

    void u16(uint16_t value) {
        data_.push_back(static_cast<uint8_t>(value));
        data_.push_back(static_cast<uint8_t>(value >> 8));
    }

    void u32(uint32_t value) {
        for (int i = 0; i < 4; i++) {
            data_.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    void u64(uint64_t value) {
        for (int i = 0; i < 8; i++) {
            data_.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    void bytes(const void *data, size_t size) {
        if (size == 0) {
            return;
        }
        const auto *begin = static_cast<const uint8_t *>(data);
        data_.insert(data_.end(), begin, begin + size);
    }

    void string(std::string_view value) {
        u32(static_cast<uint32_t>(value.size()));
        bytes(value.data(), value.size());
    }

    std::vector<uint8_t> take() { return std::move(data_); }

  private:
    std::vector<uint8_t> data_;
};

class Reader {
  public:
    Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

    bool u8(uint8_t &value) {
        if (remaining() < 1) {
            return false;
        }
        value = data_[offset_++];
        return true;
    }

    bool u16(uint16_t &value) {
        if (remaining() < 2) {
            return false;
        }
        value = static_cast<uint16_t>(data_[offset_]) |
                (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        return true;
    }

    bool u32(uint32_t &value) {
        if (remaining() < 4) {
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; i++) {
            value |= static_cast<uint32_t>(data_[offset_++]) << (i * 8);
        }
        return true;
    }

    bool u64(uint64_t &value) {
        if (remaining() < 8) {
            return false;
        }
        value = 0;
        for (int i = 0; i < 8; i++) {
            value |= static_cast<uint64_t>(data_[offset_++]) << (i * 8);
        }
        return true;
    }

    bool string(std::string &value) {
        uint32_t size = 0;
        if (!u32(size) || size > remaining()) {
            return false;
        }
        value.assign(reinterpret_cast<const char *>(data_ + offset_), size);
        offset_ += size;
        return true;
    }

    size_t remaining() const { return size_ - offset_; }

  private:
    const uint8_t *data_;
    size_t size_;
    size_t offset_ = 0;
};

inline std::vector<uint8_t> encodeKeyReply(const KeyReply &reply) {
    Writer writer;
    writer.u8(reply.consumed);
    writer.u8(reply.enabled);
    writer.string(reply.commit);
    writer.string(reply.preedit);
    writer.u32(reply.preeditCursor);
    writer.u64(reply.revision);
    writer.u32(reply.selected);
    writer.u8(reply.hasPrev);
    writer.u8(reply.hasNext);
    writer.u32(static_cast<uint32_t>(reply.candidates.size()));
    for (const auto &candidate : reply.candidates) {
        writer.string(candidate.text);
        writer.string(candidate.label);
        writer.string(candidate.comment);
    }
    return writer.take();
}

inline bool decodeKeyReply(const std::vector<uint8_t> &payload,
                           KeyReply &reply) {
    Reader reader(payload.data(), payload.size());
    KeyReply result;
    uint8_t consumed = 0, enabled = 0, hasPrev = 0, hasNext = 0;
    uint32_t count = 0;
    if (!reader.u8(consumed) || consumed > 1 || !reader.u8(enabled) ||
        enabled > 1 || !reader.string(result.commit) ||
        !reader.string(result.preedit) || !reader.u32(result.preeditCursor) ||
        result.preeditCursor > result.preedit.size() ||
        !reader.u64(result.revision) || !reader.u32(result.selected) ||
        !reader.u8(hasPrev) || hasPrev > 1 || !reader.u8(hasNext) ||
        hasNext > 1 || !reader.u32(count) || count > kMaxCandidates ||
        (result.selected != UINT32_MAX && result.selected >= count)) {
        return false;
    }
    result.consumed = consumed;
    result.enabled = enabled;
    result.hasPrev = hasPrev;
    result.hasNext = hasNext;
    result.candidates.resize(count);
    for (auto &candidate : result.candidates) {
        if (!reader.string(candidate.text) || !reader.string(candidate.label) ||
            !reader.string(candidate.comment)) {
            return false;
        }
    }
    if (reader.remaining() != 0) {
        return false;
    }
    reply = std::move(result);
    return true;
}

inline std::vector<uint8_t> encodeFrame(const Frame &frame) {
    Writer writer;
    writer.u32(kMagic);
    writer.u16(kVersion);
    writer.u16(static_cast<uint16_t>(frame.type));
    writer.u32(static_cast<uint32_t>(frame.payload.size()));
    writer.u64(frame.requestId);
    writer.u64(frame.contextId);
    writer.bytes(frame.payload.data(), frame.payload.size());
    return writer.take();
}

inline bool decodeFrame(const std::vector<uint8_t> &data, Frame &frame) {
    if (data.size() < kHeaderSize) {
        return false;
    }
    Reader reader(data.data(), data.size());
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t type = 0;
    uint32_t payloadSize = 0;
    if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(type) ||
        !reader.u32(payloadSize) || !reader.u64(frame.requestId) ||
        !reader.u64(frame.contextId) || magic != kMagic ||
        version != kVersion || payloadSize > kMaxPayloadSize ||
        payloadSize != reader.remaining()) {
        return false;
    }
    frame.type = static_cast<MessageType>(type);
    frame.payload.assign(data.end() - payloadSize, data.end());
    return true;
}

} // namespace fcitx::win32::ipc
