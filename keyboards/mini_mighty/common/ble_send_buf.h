#pragma once

#include "transfer_blob.h"

void send_buf_init(uint8_t resetPin, uint8_t sleepPin);
bool send_buf_send_one(uint16_t timeout);
bool send_buf_enqueue(const transfer_blob_t *blob);
