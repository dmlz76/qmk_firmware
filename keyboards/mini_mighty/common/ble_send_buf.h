#pragma once

#include "transfer_blob.h"

void send_buf_init(uint8_t resetPin, uint8_t sleepPin);
bool send_buf_send_one(uint16_t timeout);
bool send_buf_enqueue(const transfer_blob_t *blob);

// Like send_buf_enqueue(), but never fails: if the ring is full it drops the
// oldest queued blob to make room. The newest blob always carries the current
// HID state, so dropping the oldest can only lose a transient intermediate
// report, never leave a key/button stuck. Returns true if an entry was dropped.
bool send_buf_force_enqueue(const transfer_blob_t *blob);

#ifdef MM_SWD_BRIDGE_ENABLE
// For common/swd_bridge.c, which borrows the nRF for an SWD session.
//
// Wakes the nRF if it was put to sleep, waits out its reset/init window (~110 ms at most),
// and from then on leaves it alone: no SPI, no BLE-off, until send_buf_nrf_release(). The
// kill switch is still polled. False, and nothing claimed, if the switch holds the nRF in
// reset.
bool send_buf_nrf_claim(void);
// Hands the nRF back, with a fresh BLE idle period.
void send_buf_nrf_release(void);
// Pin-resets the nRF (BLE_RESET_HOLD_MS, blocking) with SLEEP high, so it boots awake. The
// first SPI after it waits out BLE_RESET_WAIT_MS, as after any reboot. Does nothing while
// the kill switch holds the line.
void send_buf_nrf_pin_reset(void);
#endif
