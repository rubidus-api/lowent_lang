// low_blake3.h — BLAKE3-256 (S5 content addressing, SPEC-011 §12.3).
//
// Self-contained one-shot implementation (no XOF, 32-byte digest only) over the
// standard chunk tree (1 KiB chunks, 64-byte blocks, parent merging by chunk
// count). Verified against the official empty-input test vector in run_tests.
#ifndef LOW_BLAKE3_H
#define LOW_BLAKE3_H

#include "proven/types.h"

void low_blake3_256(const proven_byte_t *data, proven_size_t len, proven_u8 out[32]);

#endif // LOW_BLAKE3_H
