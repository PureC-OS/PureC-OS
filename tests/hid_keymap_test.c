#include "drivers/input/hid_keymap.h"
#include "drivers/input/key_event.h"

#include <assert.h>

static void test_set1(void) {
  assert(hid_from_set1(0x1E, 0) == 4);
  assert(hid_from_set1(0x01, 0) == 41);
  assert(hid_from_set1(0x39, 0) == 44);
  assert(hid_from_set1(0x2A, 0) == 225);
  assert(hid_from_set1(0x1D, 0) == 224);
  assert(hid_from_set1(0x1D, 1) == 228);
  assert(hid_from_set1(0x38, 1) == 230);
  assert(hid_from_set1(0x48, 1) == 82);
  assert(hid_from_set1(0x47, 0) == 95);
  assert(hid_from_set1(0x47, 1) == 74);
  assert(hid_from_set1(0x58, 0) == 69);
  assert(hid_from_set1(0x7F, 0) == 0);
  assert(hid_from_set1(0xFF, 1) == 0);
}

static void test_set2(void) {
  assert(hid_from_set2(0x1C, 0) == 4);
  assert(hid_from_set2(0x76, 0) == 41);
  assert(hid_from_set2(0x29, 0) == 44);
  assert(hid_from_set2(0x12, 0) == 225);
  assert(hid_from_set2(0x14, 1) == 228);
  assert(hid_from_set2(0x75, 1) == 82);
  assert(hid_from_set2(0x75, 0) == 96);
  assert(hid_from_set2(0x83, 0) == 64);
  assert(hid_from_set2(0x12, 1) == 0);
  assert(hid_from_set2(0xFF, 0) == 0);
}

static void test_sets_agree(void) {
  static const uint8_t pairs[][2] = {
    {0x1E, 0x1C}, {0x30, 0x32}, {0x2C, 0x1A}, {0x02, 0x16},
    {0x0B, 0x45}, {0x39, 0x29}, {0x1C, 0x5A}, {0x0E, 0x66},
    {0x0F, 0x0D}, {0x3B, 0x05}, {0x44, 0x09}, {0x57, 0x78},
    {0x58, 0x07}, {0x2A, 0x12}, {0x36, 0x59}, {0x38, 0x11},
    {0x3A, 0x58}, {0x52, 0x70}, {0x4E, 0x79}, {0x56, 0x61},
  };
  for (unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
    assert(hid_from_set1(pairs[i][0], 0) == hid_from_set2(pairs[i][1], 0));
  static const uint8_t ext[][2] = {
    {0x48, 0x75}, {0x50, 0x72}, {0x4B, 0x6B}, {0x4D, 0x74},
    {0x47, 0x6C}, {0x4F, 0x69}, {0x49, 0x7D}, {0x51, 0x7A},
    {0x52, 0x70}, {0x53, 0x71}, {0x1D, 0x14}, {0x38, 0x11},
    {0x1C, 0x5A}, {0x35, 0x4A}, {0x5B, 0x1F}, {0x5C, 0x27},
  };
  for (unsigned i = 0; i < sizeof(ext) / sizeof(ext[0]); i++)
    assert(hid_from_set1(ext[i][0], 1) == hid_from_set2(ext[i][1], 1));
}

static void test_mod_bits(void) {
  assert(hid_modifier_bit(224) == KEY_MOD_LCTRL);
  assert(hid_modifier_bit(225) == KEY_MOD_LSHIFT);
  assert(hid_modifier_bit(226) == KEY_MOD_LALT);
  assert(hid_modifier_bit(227) == KEY_MOD_LGUI);
  assert(hid_modifier_bit(228) == KEY_MOD_RCTRL);
  assert(hid_modifier_bit(229) == KEY_MOD_RSHIFT);
  assert(hid_modifier_bit(230) == KEY_MOD_RALT);
  assert(hid_modifier_bit(231) == KEY_MOD_RGUI);
  assert(hid_modifier_bit(4) == 0);
  assert(hid_modifier_bit(232) == 0);
}

int main(void) {
  test_set1();
  test_set2();
  test_sets_agree();
  test_mod_bits();
  return 0;
}
