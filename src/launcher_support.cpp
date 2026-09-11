#include "launcher_support.h"
#include <esp_ota_ops.h>
#include <esp_sleep.h>

namespace LauncherSupport {
bool available() {
  const auto* running = esp_ota_get_running_partition();
  const auto* launcher = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
  if (!running || !launcher || running->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
      running->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX || launcher->address != 0x10000)
    return false;
  esp_app_desc_t description{};
  return esp_ota_get_partition_description(launcher, &description) == ESP_OK;
}

void restart() {
  // M5Launcher selects its test partition on power-on or deep-sleep wake.
  // A normal software restart boots the selected app again. Keep OTA/NVS intact.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(20000);
  esp_deep_sleep_start();
}
}
