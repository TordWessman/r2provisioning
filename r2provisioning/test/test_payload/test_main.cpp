#include <string.h>
#include <unity.h>

#include <vector>

#include "r2provisioning/ProvisioningPayload.hpp"

using namespace r2provisioning;

static std::vector<uint8_t> payload(const char *ssid, const char *pw, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    out.push_back((uint8_t)strlen(ssid));
    out.insert(out.end(), ssid, ssid + strlen(ssid));
    out.push_back((uint8_t)strlen(pw));
    out.insert(out.end(), pw, pw + strlen(pw));
    out.push_back((uint8_t)(data.size() & 0xFF));
    out.push_back((uint8_t)(data.size() >> 8));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

void setUp() {}
void tearDown() {}

void test_parse_ok_with_binary_data() {
    std::vector<uint8_t> data = {0x00, 0x01, 0xFF, 0x00};
    auto buf = payload("home", "secret123", data);
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parsePayload(buf.data(), buf.size(), 512, out));
    TEST_ASSERT_EQUAL_STRING("home", out.ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", out.password);
    TEST_ASSERT_EQUAL_UINT16(4, out.dataLength);
    TEST_ASSERT_EQUAL_MEMORY(data.data(), out.data, 4);
}

void test_parse_open_network_no_data() {
    auto buf = payload("cafe", "", {});
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parsePayload(buf.data(), buf.size(), 512, out));
    TEST_ASSERT_EQUAL_STRING("", out.password);
    TEST_ASSERT_EQUAL_UINT16(0, out.dataLength);
}

void test_parse_rejects_empty_ssid() {
    auto buf = payload("", "pw", {});
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), buf.size(), 512, out));
}

void test_parse_rejects_long_ssid() {
    auto buf = payload("123456789012345678901234567890123", "pw", {}); // 33 chars
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), buf.size(), 512, out));
}

void test_parse_rejects_truncated_and_trailing() {
    auto buf = payload("home", "pw", {1, 2, 3});
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), buf.size() - 1, 512, out));
    buf.push_back(0);
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), buf.size(), 512, out));
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), 0, 512, out));
}

void test_parse_rejects_nul_in_ssid() {
    std::vector<uint8_t> buf = {3, 'a', 0, 'b', 0, 0, 0};
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::Malformed, parsePayload(buf.data(), buf.size(), 512, out));
}

void test_parse_too_large() {
    std::vector<uint8_t> data(17, 0xAB);
    auto buf = payload("home", "pw", data);
    ProvisioningData out;
    TEST_ASSERT_EQUAL(ParseResult::TooLarge, parsePayload(buf.data(), buf.size(), 16, out));
    TEST_ASSERT_EQUAL(ParseResult::Ok, parsePayload(buf.data(), buf.size(), 17, out));
}

void test_record_round_trip() {
    const uint16_t maxData = 64;
    std::vector<uint8_t> data = {9, 0, 8, 0, 7};
    auto buf = payload("network", "password!", data);
    ProvisioningData in;
    TEST_ASSERT_EQUAL(ParseResult::Ok, parsePayload(buf.data(), buf.size(), maxData, in));

    std::vector<uint8_t> rec(recordSize(maxData));
    TEST_ASSERT_TRUE(encodeRecord(in, maxData, rec.data()));
    TEST_ASSERT_EQUAL_UINT8('R', rec[0]);
    TEST_ASSERT_EQUAL_UINT8('2', rec[1]);
    TEST_ASSERT_EQUAL_UINT8('P', rec[2]);
    TEST_ASSERT_EQUAL_UINT8('V', rec[3]);
    TEST_ASSERT_EQUAL_UINT8(5, rec[kRecordDataLengthOffset]);
    TEST_ASSERT_EQUAL_UINT8(0, rec[kRecordDataLengthOffset + 1]);
    TEST_ASSERT_EQUAL_MEMORY(data.data(), &rec[kRecordDataOffset], data.size());

    ProvisioningData out;
    TEST_ASSERT_TRUE(decodeRecord(rec.data(), maxData, out));
    TEST_ASSERT_EQUAL_STRING("network", out.ssid);
    TEST_ASSERT_EQUAL_STRING("password!", out.password);
    TEST_ASSERT_EQUAL_UINT16(5, out.dataLength);
    TEST_ASSERT_EQUAL_MEMORY(data.data(), out.data, data.size());
}

void test_record_rejects_corruption_and_blank() {
    const uint16_t maxData = 32;
    auto buf = payload("net", "", {1, 2});
    ProvisioningData in;
    parsePayload(buf.data(), buf.size(), maxData, in);
    std::vector<uint8_t> rec(recordSize(maxData));
    encodeRecord(in, maxData, rec.data());

    ProvisioningData out;
    rec[kRecordDataOffset] ^= 0xFF;
    TEST_ASSERT_FALSE(decodeRecord(rec.data(), maxData, out));

    std::vector<uint8_t> blank(recordSize(maxData), 0xFF); // erased flash
    TEST_ASSERT_FALSE(decodeRecord(blank.data(), maxData, out));
}

void test_crc32_known_value() {
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32((const uint8_t *)s, 9));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_ok_with_binary_data);
    RUN_TEST(test_parse_open_network_no_data);
    RUN_TEST(test_parse_rejects_empty_ssid);
    RUN_TEST(test_parse_rejects_long_ssid);
    RUN_TEST(test_parse_rejects_truncated_and_trailing);
    RUN_TEST(test_parse_rejects_nul_in_ssid);
    RUN_TEST(test_parse_too_large);
    RUN_TEST(test_record_round_trip);
    RUN_TEST(test_record_rejects_corruption_and_blank);
    RUN_TEST(test_crc32_known_value);
    return UNITY_END();
}
