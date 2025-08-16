#include <unity.h>
#include <string.h>

static bool isValidOtaUrl(const char* u) {
  return (strncmp(u, "http://", 7)==0) || (strncmp(u, "https://", 8)==0);
}

void test_ota_url() {
  TEST_ASSERT_TRUE(isValidOtaUrl("http://a/b.bin"));
  TEST_ASSERT_TRUE(isValidOtaUrl("https://a/b.bin"));
  TEST_ASSERT_FALSE(isValidOtaUrl("ftp://a/b.bin"));
  TEST_ASSERT_FALSE(isValidOtaUrl(""));
}

extern "C" void app_main() {
  UNITY_BEGIN();
  RUN_TEST(test_ota_url);
  UNITY_END();
}
