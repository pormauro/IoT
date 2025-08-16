#include <unity.h>
extern "C" {
  #include "config_store.h"
}

void test_crc_roundtrip() {
  config_t cfg; config_store_defaults(&cfg);
  uint32_t crc = config_crc32(&cfg, sizeof(cfg)-sizeof(uint32_t));
  TEST_ASSERT_EQUAL_UINT32(crc, cfg.crc32);
}

extern "C" void app_main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_roundtrip);
  UNITY_END();
}
