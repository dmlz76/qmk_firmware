#pragma once

#include <stdint.h>

// Battery levels of the paired peripherals, reported to the host as HID Battery Strength: one
// level for the keyboard and one for the mouse, each the lowest of its kind among the peers
// heard from recently. Hosts that read it (Linux, via upower) show it like any other device
// battery; macOS and Windows ignore it.

// Records a 'B' blob: `peer` (the GZLL pipe) is a `kind` peripheral at `level` percent.
void peer_battery_update(uint8_t peer, uint8_t kind, uint8_t level);

// Ages out peers that have gone quiet and sends the host any level that changed. Call every scan.
void peer_battery_task(void);
