#ifndef R2_PROVISIONING_PAYLOAD_HPP
#define R2_PROVISIONING_PAYLOAD_HPP

// Wire payload + EEPROM record codec for r2provisioning.
// Pure C++ (no Arduino includes) so it can be unit tested natively.

#include <stddef.h>
#include <stdint.h>

namespace r2provisioning {

static const uint8_t kMaxSsidLength = 32;
static const uint8_t kMaxPasswordLength = 64;

/// Decoded provisioning information. `data` points into the buffer it was
/// decoded from and is only valid as long as that buffer is.
struct ProvisioningData {
    char ssid[kMaxSsidLength + 1];
    char password[kMaxPasswordLength + 1];
    uint16_t dataLength;
    const uint8_t *data;
};

enum class ParseResult {
    Ok,
    Malformed, ///< Wrong framing, bad lengths, or trailing bytes.
    TooLarge   ///< Custom data longer than the configured maximum.
};

/// Largest valid wire payload for a given custom data maximum.
size_t maxPayloadSize(uint16_t maxData);

/// Parse a `POST /provision` body (little-endian):
///   u8 ssidLen (1..32) | ssid | u8 pwLen (0..64) | pw | u16 dataLen | data
ParseResult parsePayload(const uint8_t *buf, size_t len, uint16_t maxData, ProvisioningData &out);

// EEPROM record layout:
//   u32 magic 'R2PV' | u8 version | u8 ssidLen | ssid[32] | u8 pwLen | pw[64]
//   | u16 dataLen | data[maxData] | u32 crc32 (over all preceding bytes)
static const uint32_t kRecordMagic = 0x56503252; // "R2PV" when read as little-endian bytes
static const uint8_t kRecordVersion = 1;
static const size_t kRecordDataLengthOffset = 4 + 1 + 1 + kMaxSsidLength + 1 + kMaxPasswordLength;
static const size_t kRecordDataOffset = kRecordDataLengthOffset + 2;

/// Total record size in bytes.
size_t recordSize(uint16_t maxData);

/// Serialize into `out` (must hold `recordSize(maxData)` bytes). Unused bytes are zeroed.
/// Returns false if `in` does not fit.
bool encodeRecord(const ProvisioningData &in, uint16_t maxData, uint8_t *out);

/// Validate magic, version, lengths and CRC. On success `out.data` points into `rec`.
bool decodeRecord(const uint8_t *rec, uint16_t maxData, ProvisioningData &out);

/// CRC-32 (IEEE 802.3, reflected, as used by zlib).
uint32_t crc32(const uint8_t *buf, size_t len);

} // namespace r2provisioning

#endif // R2_PROVISIONING_PAYLOAD_HPP
