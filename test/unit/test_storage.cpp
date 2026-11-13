#include "../framework.h"
#include "drivers/storage.h"
#include "app/config_codec.h"
#include "nvs.h"
#include "nvs_flash.h"

static void test_missing_is_distinct_and_defaults_can_save() {
  nvs_stub::clear();
  drivers::Storage storage;
  ASSERT_TRUE(storage.begin());
  auto loaded = storage.load_config();
  ASSERT_EQ(loaded.status, app::contract::LoadStatus::Missing);
  ASSERT_EQ(storage.save_config(loaded.config).status, app::contract::SaveStatus::Saved);
}

static void test_future_blob_is_unsupported_and_byte_identical_after_edit() {
  nvs_stub::clear();
  app::contract::Config original{};
  uint8_t blob[128]{}; size_t size = 0;
  ASSERT_TRUE(app::config::encode(original, blob, sizeof(blob), size));
  blob[4] = 9; blob[5] = 0;  // valid envelope, future version
  const std::vector<uint8_t> before(blob, blob + size);
  nvs_stub::blobs["settings_v2"] = before;

  drivers::Storage storage;
  ASSERT_TRUE(storage.begin());
  auto loaded = storage.load_config();
  ASSERT_EQ(loaded.status, app::contract::LoadStatus::Unsupported);
  auto edited = loaded.config;
  edited.velocity = 110;
  ASSERT_EQ(storage.save_config(edited).status, app::contract::SaveStatus::Unsupported);
  ASSERT_TRUE(nvs_stub::blobs["settings_v2"] == before);
}

static void test_read_error_is_not_missing_and_blocks_save() {
  nvs_stub::clear();
  nvs_stub::blobs["settings_v2"] = {1, 2, 3, 4};
  nvs_stub::query_error = ESP_FAIL;
  drivers::Storage storage;
  ASSERT_TRUE(storage.begin());
  ASSERT_EQ(storage.load_config().status, app::contract::LoadStatus::IOError);
  ASSERT_EQ(storage.save_config(app::contract::Config{}).status, app::contract::SaveStatus::Unsupported);
}

int main() {
  TEST_SUITE("Storage safety");
  RUN_TEST(test_missing_is_distinct_and_defaults_can_save);
  RUN_TEST(test_future_blob_is_unsupported_and_byte_identical_after_edit);
  RUN_TEST(test_read_error_is_not_missing_and_blocks_save);
  test_print_result();
  return g_fail ? 1 : 0;
}
