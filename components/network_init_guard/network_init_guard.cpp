#include "network_init_guard.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/netif.h"
#include <cstring>

// Hook into lwIP's netif_add to prevent duplicate additions
extern "C" {

// Static flag to track if we're already in netif_add (prevents recursion)
static bool in_netif_add = false;
// Static counter for netif_add calls
static int netif_add_call_count = 0;

// Forward declaration of original lwIP netif_add
struct netif *__real_netif_add(struct netif *netif, const ip4_addr_t *ipaddr,
                                const ip4_addr_t *netmask, const ip4_addr_t *gw,
                                void *state, netif_init_fn init, netif_input_fn input);

// Our wrapper that checks for duplicate adds
struct netif *__wrap_netif_add(struct netif *netif, const ip4_addr_t *ipaddr,
                                const ip4_addr_t *netmask, const ip4_addr_t *gw,
                                void *state, netif_init_fn init, netif_input_fn input) {
  netif_add_call_count++;

  ESP_LOGD("network_init_guard", "netif_add() called (call #%d, netif=%p)", netif_add_call_count, netif);

  // Check if netif is already added
  if (netif != NULL && (netif->flags & NETIF_FLAG_ADDED)) {
    ESP_LOGW("network_init_guard", "⚠️  Netif %p already added (flags=0x%02x), skipping duplicate netif_add() call #%d",
             netif, netif->flags, netif_add_call_count);
    ESP_LOGW("network_init_guard", "   This prevents 'assert failed: netif_add' crash in ESP-IDF lwIP");
    ESP_LOGW("network_init_guard", "   Returning existing netif instead of crashing");
    return netif;  // Return existing netif instead of crashing
  }

  // Prevent recursion
  if (in_netif_add) {
    ESP_LOGE("network_init_guard", "❌ Recursive netif_add() detected! Returning NULL to prevent stack overflow");
    return NULL;
  }

  in_netif_add = true;

  // Call original netif_add
  struct netif *result = __real_netif_add(netif, ipaddr, netmask, gw, state, init, input);

  in_netif_add = false;

  if (result != NULL) {
    ESP_LOGD("network_init_guard", "✅ netif_add() call #%d succeeded, netif=%p", netif_add_call_count, result);
  } else {
    ESP_LOGE("network_init_guard", "❌ netif_add() call #%d failed, returned NULL", netif_add_call_count);
  }

  return result;
}
}
#endif

namespace esphome {
namespace network_init_guard {

static const char *const TAG = "network_init_guard";

bool NetworkInitGuard::network_initialized_ = false;

void NetworkInitGuard::setup() {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  Network Initialization Guard");
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "Status: Active");
  ESP_LOGI(TAG, "Purpose: Prevent 'netif already added' crash");
  ESP_LOGI(TAG, "Method: Wrap lwIP netif_add() to check for duplicates");

#ifdef USE_ESP_IDF
  // Log current network initialization state
  esp_netif_t *netif = esp_netif_next(NULL);
  if (netif != NULL) {
    ESP_LOGI(TAG, "Network interface already exists at startup");
    network_initialized_ = true;
  } else {
    ESP_LOGI(TAG, "No network interface found yet (expected)");
  }

  ESP_LOGI(TAG, "========================================");
#else
  ESP_LOGW(TAG, "Not using ESP-IDF - guard inactive");
#endif
}

float NetworkInitGuard::get_setup_priority() const {
  // Run BEFORE WiFi component (which typically has priority 250)
  // But AFTER bus components
  return setup_priority::WIFI - 10.0f;  // 240
}

void NetworkInitGuard::dump_config() {
  ESP_LOGCONFIG(TAG, "Network Init Guard:");
  ESP_LOGCONFIG(TAG, "  Status: Active (prevents netif_add crash)");
  ESP_LOGCONFIG(TAG, "  Network initialized: %s", network_initialized_ ? "yes" : "no");
}

}  // namespace network_init_guard
}  // namespace esphome
