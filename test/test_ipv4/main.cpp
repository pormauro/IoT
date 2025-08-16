#include <unity.h>
#include <string>
#include <vector>
extern "C" {
  #include "config_store.h"
}

static bool ipV4Valid(const char* ip) {
  int a,b,c,d; char tail;
  if (sscanf(ip, "%d.%d.%d.%d%c", &a,&b,&c,&d,&tail) != 4) return false;
  return a>=0&&a<=255 && b>=0&&b<=255 && c>=0&&c<=255 && d>=0&&d<=255;
}

void test_ipv4_valid() {
  TEST_ASSERT_TRUE(ipV4Valid("192.168.1.10"));
  TEST_ASSERT_TRUE(ipV4Valid("0.0.0.0"));
  TEST_ASSERT_TRUE(ipV4Valid("255.255.255.255"));
}

void test_ipv4_invalid() {
  TEST_ASSERT_FALSE(ipV4Valid("256.1.1.1"));
  TEST_ASSERT_FALSE(ipV4Valid("192.168.1"));
  TEST_ASSERT_FALSE(ipV4Valid("192.168.1.a"));
}

extern "C" void app_main() {
  UNITY_BEGIN();
  RUN_TEST(test_ipv4_valid);
  RUN_TEST(test_ipv4_invalid);
  UNITY_END();
}
