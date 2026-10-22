/* ===================================================================================================
 * utils/include/utils/mailbox/mailbox.h
 *
 * Latest-wins mailbox: one slot holding the most recent value. A write
 * replaces whatever was there, read or not. Neither side ever blocks.
 *
 * Threads:
 *  - mb_write:   ONE writer thread only
 *  - mb_read:    ONE reader thread only
 *  - mb_destroy: only once both sides have stopped using it
 *
 * Memory ordering contract (seqlock, C11 <stdatomic.h>):
 *  - A sequence counter is odd while a write is in progress and even when the
 *    slot is stable. Each completed write adds 2. Version = sequence / 2.
 *  - Writer: sequence + 1 (odd), release fence, store the payload words,
 *    release-store sequence + 2 (even).
 *  - Reader: acquire-load the sequence, retry while odd; load the payload
 *    words; acquire fence; reload the sequence and retry if it changed (a
 *    write overlapped the copy).
 *  - The payload is an array of _Atomic uint64_t accessed relaxed, so even a
 *    copy that overlaps a write is not a C11 data race: it is detected by the
 *    sequence check and thrown away. This is why elem_size must be a multiple
 *    of 8 (pad Forecast, Led_Command, ... to a multiple of 8).
 *  - The reader may retry, but only while a write is actually overlapping its
 *    copy. The writer never waits.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_UTILS_MAILBOX_MAILBOX_H
#define MTS_UTILS_MAILBOX_MAILBOX_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque: layout, padding and atomics live in mailbox.c
typedef struct Mailbox Mailbox;

/* elem_size must be > 0 and a multiple of 8. The slot starts empty.
 * Returns NULL (and logs an error) on a bad size or if malloc fails. */
Mailbox* mb_create(size_t elem_size);

// Frees the mailbox. NULL is fine.
void mb_destroy(Mailbox* mb);

/* Writer only. Copies elem_size bytes from elem into the slot, replacing
 * the previous value. Never blocks. */
void mb_write(Mailbox* mb, const void* elem);

/* Reader only. true = a value was written since this reader's last
 * successful read: out filled with the latest one. false = nothing new
 * (or never written): out untouched. Values written in between and
 * overwritten before this read are skipped, by design. */
bool mb_read(Mailbox* mb, void* out);

#ifdef __cplusplus
}
#endif

#endif /* MTS_UTILS_MAILBOX_MAILBOX_H */

