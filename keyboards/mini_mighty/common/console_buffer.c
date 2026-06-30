// Copyright 2025 Dimitrix LLC
// SPDX-License-Identifier: GPL-2.0-or-later
#include "console_buffer.h"

#ifdef CONSOLE_ENABLE
#    include "print.h"
#    include "sendchar.h"

// Ring buffer size in bytes — must be a power of two. Sized to hold the debug
// output produced while a listener is detached (a few dozen lines).
#    ifndef CONSOLE_BUFFER_SIZE
#        define CONSOLE_BUFFER_SIZE 512
#    endif
#    if (CONSOLE_BUFFER_SIZE & (CONSOLE_BUFFER_SIZE - 1)) != 0
#        error "CONSOLE_BUFFER_SIZE must be a power of two"
#    endif
#    define CONSOLE_BUFFER_MASK (CONSOLE_BUFFER_SIZE - 1)

// Max bytes drained per flush call. Bounds the work (and the worst-case
// sendchar() endpoint-wait) done in a single housekeeping tick; the rest drains
// over subsequent ticks.
#    ifndef CONSOLE_FLUSH_CHUNK
#        define CONSOLE_FLUSH_CHUNK 64
#    endif

static uint8_t  s_buf[CONSOLE_BUFFER_SIZE];
static uint16_t s_head; // next write index
static uint16_t s_tail; // next read index

// Producer (uprintf -> putchar_ -> here) and consumer (the flush task) both run
// in the cooperative main loop and never preempt each other, so the indices need
// no locking. (Nothing in this firmware prints from an ISR.)
static int8_t console_buffer_sendchar(uint8_t c) {
    uint16_t next = (s_head + 1) & CONSOLE_BUFFER_MASK;
    if (next == s_tail) {
        // Full: drop the oldest byte so the most recent output (the event we're
        // chasing) is always kept.
        s_tail = (s_tail + 1) & CONSOLE_BUFFER_MASK;
    }
    s_buf[s_head] = c;
    s_head        = next;
    return 0; // queued — never dropped to the void
}

void console_buffer_init(void) {
    print_set_sendchar(console_buffer_sendchar);
}

void console_buffer_flush_task(void) {
    for (uint8_t n = 0; n < CONSOLE_FLUSH_CHUNK && s_tail != s_head; n++) {
        // sendchar() returns < 0 when USB isn't configured or the listener is
        // gone; leave the rest queued and retry next tick.
        if (sendchar(s_buf[s_tail]) < 0) {
            return;
        }
        s_tail = (s_tail + 1) & CONSOLE_BUFFER_MASK;
    }
}

#else // !CONSOLE_ENABLE

void console_buffer_init(void) {}
void console_buffer_flush_task(void) {}

#endif
