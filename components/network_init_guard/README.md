# Network Initialization Guard Component

## Purpose

Prevents the "assert failed: netif_add /IDF/components/lwip/lwip/src/core/netif.c:420 (netif already added)" crash that occurs on ESP32-P4 with ESP-IDF 5.4.x when running ESPHome.

## The Problem

The ESP32-P4 experiences a crash during startup when lwIP's `netif_add()` function is called twice on the same network interface structure. This happens due to:

- Race condition in network initialization
- WiFi reconnection logic not properly cleaning up network interfaces
- Component setup ordering issues in ESPHome with ESP-IDF 5.4.x

The crash looks like this:
```
assert failed: netif_add /IDF/components/lwip/lwip/src/core/netif.c:420 (netif already added)
Core  0 register dump:
MEPC    : 0x4ff02f28  RA      : 0x4ff0a384  SP      : 0x4ff46d40
...
Rebooting...
```

## The Solution

This component wraps lwIP's `netif_add()` function using GNU linker's `--wrap` feature. When `netif_add()` is called, our wrapper function:

1. Checks if the network interface is already added (NETIF_FLAG_ADDED flag is set)
2. If already added, returns the existing netif instead of calling the real `netif_add()` again
3. If not added, calls the original `netif_add()` function
4. Tracks call count and prevents recursion

## Usage

Add this component to your ESPHome YAML configuration:

```yaml
# Before WiFi component loads
network_init_guard:
  # No configuration options needed

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

The component automatically:
- Loads before the WiFi component (setup priority: 240)
- Wraps the `netif_add()` function via linker flags
- Logs all `netif_add()` calls for debugging
- Prevents duplicate network interface registration

## How It Works

### Linker Wrapping

The `__init__.py` file adds the linker flag:
```python
cg.add_build_flag("-Wl,--wrap=netif_add")
```

This tells the linker to:
1. Rename the original `netif_add()` to `__real_netif_add()`
2. Use our `__wrap_netif_add()` function instead

### Function Wrapper

Our wrapper in `network_init_guard.cpp`:
```c
struct netif *__wrap_netif_add(struct netif *netif, ...) {
  // Check if already added
  if (netif != NULL && (netif->flags & NETIF_FLAG_ADDED)) {
    ESP_LOGW(..., "Netif already added, skipping duplicate call");
    return netif;  // Return existing instead of crashing
  }

  // Call original function
  return __real_netif_add(netif, ...);
}
```

## Expected Logs

When the component is active, you'll see:

```
[network_init_guard]: ========================================
[network_init_guard]:   Network Initialization Guard
[network_init_guard]: ========================================
[network_init_guard]: Status: Active
[network_init_guard]: Purpose: Prevent 'netif already added' crash
[network_init_guard]: Method: Wrap lwIP netif_add() to check for duplicates
[network_init_guard]: No network interface found yet (expected)
[network_init_guard]: ========================================
```

If a duplicate call is detected:
```
[network_init_guard]: ⚠️  Netif 0x... already added (flags=0x...), skipping duplicate netif_add() call #2
[network_init_guard]:    This prevents 'assert failed: netif_add' crash in ESP-IDF lwIP
[network_init_guard]:    Returning existing netif instead of crashing
```

## Technical Details

- **Setup Priority**: 240 (runs before WiFi component at priority 250)
- **Dependencies**: ESP-IDF framework only
- **Linker Flags**: `--wrap=netif_add`
- **Include Paths**: lwIP headers from ESP-IDF components

## Limitations

- Only works with ESP-IDF framework (not Arduino)
- Requires GNU ld linker (standard in ESP-IDF)
- Does not fix the root cause, only prevents the crash

## Future Improvements

The root cause should be fixed in either:
1. ESPHome's WiFi component to prevent duplicate initialization
2. ESP-IDF's lwIP integration to handle re-initialization gracefully
3. Network interface cleanup logic during WiFi reconnection

## References

- ESP-IDF lwIP component: `components/lwip/lwip/src/core/netif.c`
- ESPHome WiFi component: Core WiFi handling
- GNU ld wrapping: `--wrap` linker option
