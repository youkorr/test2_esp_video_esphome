#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace network_init_guard {

/**
 * @brief Component to prevent duplicate network interface initialization
 *
 * This component works around an ESP-IDF issue where netif_add() can be called
 * twice, causing "assert failed: netif_add /IDF/components/lwip/lwip/src/core/netif.c:420
 * (netif already added)" crash.
 *
 * This is a high-priority component that sets up before WiFi to ensure proper
 * network initialization order.
 */
class NetworkInitGuard : public Component {
 public:
  void setup() override;
  float get_setup_priority() const override;
  void dump_config() override;

 private:
  static bool network_initialized_;
};

}  // namespace network_init_guard
}  // namespace esphome
