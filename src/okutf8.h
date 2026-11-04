#ifndef OK_UTF8_H
#define OK_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ok_utf8 (P2, the 0.9.0 codepoint-first amendment): the whole utf-8 story
// in one header - the C99 port of the ok::utf8 design sketch, with the old
// FIXME resolved into a contract:
//   - decode answers 0 on malformed input. callers walking VALIDATED
//     regions (any source that passed the front door) may assert on 0 -
//     it cannot happen there. runtime strings (fs reads, env, argv) are
//     NOT validated: the lossy ruling stands, one U+FFFD per malformed
//     sequence at the codepoint operations, never a crash, never a
//     rejection at construction.
//   - validate() is the ONLY entry that may see bad bytes. it rejects:
//     malformed / overlong (C0, C1, E0 80-9F, F0 80-8F) / surrogate
//     (ED A0-BF) / out-of-range (F5+, F4 90+) sequences, raw NUL, the
//     bidi controls (U+202A-202E and U+2066-2069, the trojan-source
//     hole), and C0 bytes other than \t \n \r.
//   - failure carries the byte offset; the parser turns it into a
//     located parse error (the diagnostic doctrine).
// the lexer never learns unicode exists: the front door guarantees
// well-formedness, identifiers accept any non-ascii codepoint, and only
// the three codepoint operations (len, iteration, index) ever decode.

typedef enum {
  OK_UTF8_FINE = 0,
  OK_UTF8_MALFORMED, // bad lead byte, bad continuation, overlong, surrogate, past U+10FFFF
  OK_UTF8_NUL,       // a raw NUL byte in the source
  OK_UTF8_BIDI,      // a bidirectional text control (trojan source)
  OK_UTF8_CONTROL,   // a C0 byte other than \t \n \r
} ok_utf8_rejection;

static inline const char* ok_utf8_rejection_text(ok_utf8_rejection p_why) {
  switch (p_why) {
  case OK_UTF8_MALFORMED:
    return "malformed utf-8";
  case OK_UTF8_NUL:
    return "a raw NUL byte";
  case OK_UTF8_BIDI:
    return "a bidirectional text control";
  case OK_UTF8_CONTROL:
    return "a control byte (only tab, newline, and return are allowed)";
  default:
    return "fine";
  }
}

// decode one codepoint at p_at with p_remaining bytes left. *p_length
// takes the sequence length (1 on any malformed input, so callers can
// advance lossily). returns the codepoint, or 0 on malformed input - 0 is
// unreachable for valid utf-8, so validated-region callers may assert.
static inline uint32_t ok_utf8_decode(const char* p_at, size_t p_remaining, uint8_t* p_length) {
  const uint8_t b0 = (uint8_t)p_at[0];
  if (b0 < 0x80u) {
    *p_length = 1;
    return b0;
  }
  uint8_t need = 0;
  uint32_t cp = 0;
  uint8_t lo = 0x80u; // the FIRST continuation's legal window
  uint8_t hi = 0xBFu;
  if (b0 >= 0xC2u && b0 <= 0xDFu) {
    need = 1;
    cp = b0 & 0x1Fu;
  } else if (b0 == 0xE0u) {
    need = 2;
    cp = b0 & 0x0Fu;
    lo = 0xA0u; // E0 80-9F would be overlong
  } else if ((b0 >= 0xE1u && b0 <= 0xECu) || (b0 >= 0xEEu && b0 <= 0xEFu)) {
    need = 2;
    cp = b0 & 0x0Fu;
  } else if (b0 == 0xEDu) {
    need = 2;
    cp = b0 & 0x0Fu;
    hi = 0x9Fu; // ED A0-BF would be a surrogate
  } else if (b0 == 0xF0u) {
    need = 3;
    cp = b0 & 0x07u;
    lo = 0x90u; // F0 80-8F would be overlong
  } else if (b0 >= 0xF1u && b0 <= 0xF3u) {
    need = 3;
    cp = b0 & 0x07u;
  } else if (b0 == 0xF4u) {
    need = 3;
    cp = b0 & 0x07u;
    hi = 0x8Fu; // F4 90+ would be past U+10FFFF
  } else {
    // 0x80-BF (stray continuation), 0xC0/0xC1 (overlong), 0xF5-FF (out of range)
    *p_length = 1;
    return 0;
  }
  if ((size_t)need + 1u > p_remaining) {
    *p_length = 1;
    return 0; // truncated sequence at the source end
  }
  const uint8_t b1 = (uint8_t)p_at[1];
  if (b1 < lo || b1 > hi) {
    *p_length = 1;
    return 0;
  }
  cp = (cp << 6) | (b1 & 0x3Fu);
  for (uint8_t i = 2; i <= need; ++i) {
    const uint8_t bi = (uint8_t)p_at[i];
    if (bi < 0x80u || bi > 0xBFu) {
      *p_length = 1;
      return 0;
    }
    cp = (cp << 6) | (bi & 0x3Fu);
  }
  *p_length = (uint8_t)(need + 1u);
  return cp;
}

// the front door: validate a whole source region. false + the byte offset
// and the reason of the first rejection.
static inline bool
ok_utf8_validate(const char* p_src, size_t p_length, uint32_t* p_offset, ok_utf8_rejection* p_why) {
  size_t i = 0;
  while (i < p_length) {
    const uint8_t b = (uint8_t)p_src[i];
    if (b == 0x00u) {
      *p_offset = (uint32_t)i;
      *p_why = OK_UTF8_NUL;
      return false;
    }
    if (b < 0x20u && b != '\t' && b != '\n' && b != '\r') {
      *p_offset = (uint32_t)i;
      *p_why = OK_UTF8_CONTROL;
      return false;
    }
    if (b < 0x80u) {
      ++i;
      continue;
    }
    uint8_t len = 0;
    const uint32_t cp = ok_utf8_decode(p_src + i, p_length - i, &len);
    if (cp == 0) {
      *p_offset = (uint32_t)i;
      *p_why = OK_UTF8_MALFORMED;
      return false;
    }
    if ((cp >= 0x202Au && cp <= 0x202Eu) || (cp >= 0x2066u && cp <= 0x2069u)) {
      *p_offset = (uint32_t)i;
      *p_why = OK_UTF8_BIDI;
      return false;
    }
    i += len;
  }
  return true;
}

// the lossy count: codepoints in possibly-malformed data - one U+FFFD per
// malformed byte, so len() and the codepoint cursor never crash and never
// reject at construction (the runtime-string ruling).
static inline uint64_t ok_utf8_count_lossy(const char* p_src, size_t p_length) {
  uint64_t count = 0;
  size_t i = 0;
  while (i < p_length) {
    const uint8_t b = (uint8_t)p_src[i];
    if (b < 0x80u) {
      ++count;
      ++i;
      continue;
    }
    uint8_t len = 0;
    const uint32_t cp = ok_utf8_decode(p_src + i, p_length - i, &len);
    ++count;      // the codepoint, or this byte's U+FFFD
    i += len;     // len is 1 on malformed: one replacement per bad byte
    (void)cp;
  }
  return count;
}

// encode one codepoint (p_out needs >= 4 bytes of room). returns the
// written length, 0 if the codepoint is out of range or a surrogate.
static inline uint8_t ok_utf8_encode(uint32_t p_cp, char* p_out) {
  if (p_cp > 0x10FFFFu || (p_cp >= 0xD800u && p_cp <= 0xDFFFu)) {
    return 0;
  }
  if (p_cp < 0x80u) {
    p_out[0] = (char)p_cp;
    return 1;
  }
  if (p_cp < 0x800u) {
    p_out[0] = (char)(0xC0u | (p_cp >> 6));
    p_out[1] = (char)(0x80u | (p_cp & 0x3Fu));
    return 2;
  }
  if (p_cp < 0x10000u) {
    p_out[0] = (char)(0xE0u | (p_cp >> 12));
    p_out[1] = (char)(0x80u | ((p_cp >> 6) & 0x3Fu));
    p_out[2] = (char)(0x80u | (p_cp & 0x3Fu));
    return 3;
  }
  p_out[0] = (char)(0xF0u | (p_cp >> 18));
  p_out[1] = (char)(0x80u | ((p_cp >> 12) & 0x3Fu));
  p_out[2] = (char)(0x80u | ((p_cp >> 6) & 0x3Fu));
  p_out[3] = (char)(0x80u | (p_cp & 0x3Fu));
  return 4;
}

#endif // OK_UTF8_H
