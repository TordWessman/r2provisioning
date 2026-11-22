#include "ProvisioningPayload.hpp"

#include <string.h>

namespace r2provisioning {

namespace {

void writeU16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t readU16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void writeU32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

uint32_t readU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Copy a length-prefixed string into a NUL-terminated buffer. Embedded NULs are
// rejected since SSID/password are handled as C strings.
bool copyString(const uint8_t *src, uint8_t len, uint8_t maxLen, char *dst) {
    if (len > maxLen || memchr(src, 0, len) != nullptr) {
        return false;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return true;
}

} // namespace

size_t maxPayloadSize(uint16_t maxData) {
    return 1 + kMaxSsidLength + 1 + kMaxPasswordLength + 2 + maxData;
}

ParseResult parsePayload(const uint8_t *buf, size_t len, uint16_t maxData, ProvisioningData &out) {
    size_t pos = 0;

    if (buf == nullptr || len < 1) { return ParseResult::Malformed; }
    uint8_t ssidLen = buf[pos++];
    if (ssidLen == 0 || pos + ssidLen > len) { return ParseResult::Malformed; }
    if (!copyString(buf + pos, ssidLen, kMaxSsidLength, out.ssid)) { return ParseResult::Malformed; }
    pos += ssidLen;

    if (pos + 1 > len) { return ParseResult::Malformed; }
    uint8_t pwLen = buf[pos++];
    if (pos + pwLen > len) { return ParseResult::Malformed; }
    if (!copyString(buf + pos, pwLen, kMaxPasswordLength, out.password)) { return ParseResult::Malformed; }
    pos += pwLen;

    if (pos + 2 > len) { return ParseResult::Malformed; }
    uint16_t dataLen = readU16(buf + pos);
    pos += 2;
    if (dataLen > maxData) { return ParseResult::TooLarge; }
    if (pos + dataLen != len) { return ParseResult::Malformed; }

    out.dataLength = dataLen;
    out.data = buf + pos;
    return ParseResult::Ok;
}

size_t recordSize(uint16_t maxData) {
    return kRecordDataOffset + maxData + 4;
}

bool encodeRecord(const ProvisioningData &in, uint16_t maxData, uint8_t *out) {
    size_t ssidLen = strlen(in.ssid);
    size_t pwLen = strlen(in.password);
    if (ssidLen == 0 || ssidLen > kMaxSsidLength || pwLen > kMaxPasswordLength || in.dataLength > maxData) {
        return false;
    }
    if (in.dataLength > 0 && in.data == nullptr) {
        return false;
    }

    size_t size = recordSize(maxData);
    memset(out, 0, size);

    uint8_t *p = out;
    writeU32(p, kRecordMagic);
    p += 4;
    *p++ = kRecordVersion;
    *p++ = static_cast<uint8_t>(ssidLen);
    memcpy(p, in.ssid, ssidLen);
    p += kMaxSsidLength;
    *p++ = static_cast<uint8_t>(pwLen);
    memcpy(p, in.password, pwLen);
    p += kMaxPasswordLength;
    writeU16(p, in.dataLength);
    p += 2;
    if (in.dataLength > 0) {
        memcpy(p, in.data, in.dataLength);
    }

    writeU32(out + size - 4, crc32(out, size - 4));
    return true;
}

bool decodeRecord(const uint8_t *rec, uint16_t maxData, ProvisioningData &out) {
    size_t size = recordSize(maxData);
    if (readU32(rec) != kRecordMagic || rec[4] != kRecordVersion) {
        return false;
    }
    if (readU32(rec + size - 4) != crc32(rec, size - 4)) {
        return false;
    }

    const uint8_t *p = rec + 5;
    uint8_t ssidLen = *p++;
    if (ssidLen == 0 || !copyString(p, ssidLen, kMaxSsidLength, out.ssid)) { return false; }
    p += kMaxSsidLength;
    uint8_t pwLen = *p++;
    if (!copyString(p, pwLen, kMaxPasswordLength, out.password)) { return false; }

    uint16_t dataLen = readU16(rec + kRecordDataLengthOffset);
    if (dataLen > maxData) { return false; }
    out.dataLength = dataLen;
    out.data = rec + kRecordDataOffset;
    return true;
}

uint32_t crc32(const uint8_t *buf, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

} // namespace r2provisioning
