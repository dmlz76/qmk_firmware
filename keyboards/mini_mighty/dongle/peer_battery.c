#include "peer_battery.h"

#include <stdbool.h>
#include "debug.h"
#include "host.h"
#include "timer.h"
#include "usb_device_state.h"
#include "transfer_blob.h"

// One slot per GZLL pipe. The dongle's nRF hands out pipes 1..MM_PAIR_MAX_PEERS (4 today); 8
// covers every pipe Gazell has, so raising that constant needs nothing here.
#define PEER_SLOTS 8

// Timestamps are timer_read32() >> 10: ~1 s ticks that fit a uint16_t, halving the table. They
// wrap every ~18 h, which is fine for a timeout checked on every scan.
#define TICK_SHIFT 10

// A peer that sends nothing for this long stops counting toward the minimum. Otherwise a second
// mouse that was switched off, or carried out of range, would hold the reported level at whatever
// it last said, forever. Peripherals resend every 2 min while their nRF is awake, idle or not, so
// this survives two lost refreshes. A peripheral's nRF is put to sleep after 5 min without input
// (BLE_OFF_TIMEOUT_MS in common/ble_send_buf.cpp), so a peer drops out 3-5 min after it sleeps,
// and the minimum is effectively over devices in use. Dropping out costs nothing: the host keeps
// showing the last level it was sent, and the peer reports again within seconds of waking.
#define PEER_TIMEOUT_TICKS ((5UL * 60 * 1000) >> TICK_SHIFT)

// How long after USB (re)configures to hold off sending. Until the host starts polling, a report
// just sits in the endpoint's only bank, and the next one on that endpoint (a real keypress) times
// out behind it in send_report() and is lost.
#define USB_SETTLE_MS 1000

typedef struct {
    uint8_t  kind;  // 'M', 'K', or 0 for an empty slot
    uint8_t  level; // 0..100 %, or TRANSFER_BATTERY_NONE
    uint16_t heard; // tick of the last 'B' from this peer
} peer_t;

static peer_t peers[PEER_SLOTS];

// What the host holds, per kind. Reset to HID_BATTERY_UNKNOWN whenever USB isn't configured:
// that covers a fresh enumeration, after which the host's battery starts out unknown, and a level
// that changed while the host was suspended and so never arrived.
static uint8_t sent_keyboard = HID_BATTERY_UNKNOWN;
static uint8_t sent_mouse    = HID_BATTERY_UNKNOWN;

// Last time USB was seen not configured, i.e. roughly when it was, once it is.
static uint16_t configured_at;
static bool     settled;

static uint16_t now_ticks(void) {
    return (uint16_t)(timer_read32() >> TICK_SHIFT);
}

void peer_battery_update(uint8_t peer, uint8_t kind, uint8_t level) {
    if (peer >= PEER_SLOTS || (kind != 'M' && kind != 'K')) {
        return;
    }
    peers[peer] = (peer_t){.kind = kind, .level = level, .heard = now_ticks()};
}

// The lowest level among `kind` peers, or HID_BATTERY_UNKNOWN when none has one. That sentinel is
// above every real level, so it doubles as the starting point of the min. TRANSFER_BATTERY_NONE
// (on external power) is above 100 as well and never counts.
static uint8_t lowest(uint8_t kind) {
    uint8_t low = HID_BATTERY_UNKNOWN;
    for (uint8_t i = 0; i < PEER_SLOTS; i++) {
        if (peers[i].kind == kind && peers[i].level <= 100 && peers[i].level < low) {
            low = peers[i].level;
        }
    }
    return low;
}

void peer_battery_task(void) {
    uint16_t now = now_ticks();
    for (uint8_t i = 0; i < PEER_SLOTS; i++) {
        if (peers[i].kind && (uint16_t)(now - peers[i].heard) > PEER_TIMEOUT_TICKS) {
            peers[i].kind = 0;
        }
    }

    if (usb_device_state_get_configure_state() != USB_DEVICE_STATE_CONFIGURED) {
        sent_keyboard = HID_BATTERY_UNKNOWN;
        sent_mouse    = HID_BATTERY_UNKNOWN;
        configured_at = timer_read();
        settled       = false;
        return;
    }
    if (!settled) {
        if (timer_elapsed(configured_at) < USB_SETTLE_MS) {
            return;
        }
        settled = true;
    }

    // An unknown level is never sent. Once every peer of a kind has gone quiet, the host keeps
    // showing the last real level, which is the best answer available.
    uint8_t level = lowest('K');
    if (level != HID_BATTERY_UNKNOWN && level != sent_keyboard) {
        dprintf("battery: keyboard %u%%\n", level);
        host_keyboard_battery_send(level);
        sent_keyboard = level;
    }

    level = lowest('M');
    if (level != HID_BATTERY_UNKNOWN && level != sent_mouse) {
        dprintf("battery: mouse %u%%\n", level);
        host_mouse_battery_send(level);
        sent_mouse = level;
    }
}
