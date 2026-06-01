#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "test_helpers.h"
#include "test_vectors.h"

using namespace esphome::samsung_aqv;

static constexpr size_t NUM_VECTORS = sizeof(VECTORS) / sizeof(VECTORS[0]);

// --- Encode tests ---

TEST_CASE("encode_off matches expected") { CHECK(encode_off() == EXPECTED_OFF); }

TEST_CASE("encode_on matches all 456 vectors") {
  for (size_t i = 0; i < NUM_VECTORS; i++) {
    auto &v = VECTORS[i];
    CAPTURE(i);
    CHECK(encode_on(v.temp, v.mode, v.fan, v.swing) == std::string(v.pronto));
  }
}

// --- Decode from Pronto tests ---

TEST_CASE("decode_pronto OFF") {
  auto d = decode_pronto(EXPECTED_OFF);
  CHECK(d.valid);
  CHECK(d.is_off);
}

TEST_CASE("decode_pronto roundtrip all 456 vectors") {
  for (size_t i = 0; i < NUM_VECTORS; i++) {
    auto &v = VECTORS[i];
    CAPTURE(i);
    auto d = decode_pronto(v.pronto);
    CHECK(d.valid);
    CHECK_FALSE(d.is_off);
    CHECK(d.mode == v.mode);
    CHECK(d.fan == v.fan);
    CHECK(d.swing == v.swing);
    CHECK(d.temp == v.temp);
  }
}

// --- Decode from timings tests ---

TEST_CASE("decode_from_timings roundtrip all 456 vectors") {
  for (size_t i = 0; i < NUM_VECTORS; i++) {
    auto &v = VECTORS[i];
    CAPTURE(i);
    auto timings = pronto_to_timings(v.pronto);
    REQUIRE(!timings.empty());
    auto d = decode_from_timings(timings.data(), timings.size());
    CHECK(d.valid);
    CHECK_FALSE(d.is_off);
    CHECK(d.mode == v.mode);
    CHECK(d.fan == v.fan);
    CHECK(d.swing == v.swing);
    CHECK(d.temp == v.temp);
  }
}

TEST_CASE("decode OFF") {
  auto d = decode_pronto(EXPECTED_OFF);
  CHECK(d.valid);
  CHECK(d.is_off);
}

// --- Fan fallback tests ---

TEST_CASE("resolve_fan: dry forces auto") {
  CHECK(resolve_fan(MODE_DRY, FAN_LOW) == FAN_AUTO);
  CHECK(resolve_fan(MODE_DRY, FAN_HIGH) == FAN_AUTO);
  CHECK(resolve_fan(MODE_DRY, FAN_QUIET) == FAN_AUTO);
}

TEST_CASE("resolve_fan: fan_only disallows auto/quiet") {
  CHECK(resolve_fan(MODE_FAN_ONLY, FAN_AUTO) == FAN_LOW);
  CHECK(resolve_fan(MODE_FAN_ONLY, FAN_QUIET) == FAN_LOW);
  CHECK(resolve_fan(MODE_FAN_ONLY, FAN_HIGH) == FAN_HIGH);
}

TEST_CASE("resolve_fan: heat_cool disallows quiet") {
  CHECK(resolve_fan(MODE_HEAT_COOL, FAN_QUIET) == FAN_AUTO);
  CHECK(resolve_fan(MODE_HEAT_COOL, FAN_LOW) == FAN_LOW);
}

TEST_CASE("resolve_fan: cool/heat allow all") {
  CHECK(resolve_fan(MODE_COOL, FAN_QUIET) == FAN_QUIET);
  CHECK(resolve_fan(MODE_HEAT, FAN_HIGH) == FAN_HIGH);
}

// --- Edge cases ---

TEST_CASE("decode_pronto invalid input") {
  CHECK_FALSE(decode_pronto("").valid);
  CHECK_FALSE(decode_pronto("0000").valid);
  CHECK_FALSE(decode_pronto("garbage data here").valid);
}

TEST_CASE("decode_from_timings too short") {
  int32_t t[] = {8900, 450};
  CHECK_FALSE(decode_from_timings(t, 2).valid);
}

TEST_CASE("encode boundary temperatures") {
  auto d16 = decode_pronto(encode_on(16, MODE_COOL, FAN_AUTO, SWING_OFF));
  CHECK(d16.temp == 16);
  auto d30 = decode_pronto(encode_on(30, MODE_COOL, FAN_AUTO, SWING_OFF));
  CHECK(d30.temp == 30);
}

TEST_CASE("decode rejects corrupted checksum") {
  // Get a valid pronto, corrupt a checksum bit, verify rejection
  std::string pronto = encode_on(24, MODE_COOL, FAN_AUTO, SWING_OFF);
  // Burst 2 checksum is at bits 12-16 (Pronto pairs starting at offset for burst 2)
  // Simpler: corrupt a data bit in burst 2 (bit 36 = temp LSB) without fixing checksum
  // Find the burst 2 temp bit position and flip it
  auto timings = pronto_to_timings(pronto);
  REQUIRE(!timings.empty());
  // Flip a data space value (make a '0' space look like '1')
  // Burst 2 starts at: hdr_mark + hdr_space + 56*(mark+space) + trailing + inter_space + inter_mark + inter_space
  // = 1 + 1 + 112 + 1 + 1 + 1 + 1 = 118, then bit 36 is at 118 + 36*2 + 1 = 191 (the space)
  size_t bit36_space_idx = 118 + 36 * 2 + 1;
  REQUIRE(bit36_space_idx < timings.size());
  timings[bit36_space_idx] = 1630;  // force to '1' regardless of original
  auto d = decode_from_timings(timings.data(), timings.size());
  // Should fail checksum (we changed data without updating checksum)
  CHECK_FALSE(d.valid);
}
