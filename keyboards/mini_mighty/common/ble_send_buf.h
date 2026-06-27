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
