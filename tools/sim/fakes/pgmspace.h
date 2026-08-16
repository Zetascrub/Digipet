#pragma once

// On the real target, PROGMEM keeps constant tables in flash instead of
// RAM, and pgm_read_*() go through a flash-cache-aware read. On a native
// host build there's one flat address space, so PROGMEM is a no-op and
// pgm_read_*() is a plain dereference -- same values, same behavior.

#define PROGMEM

#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t *>(addr))
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t *>(addr))
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t *>(addr))
#define pgm_read_float(addr) (*reinterpret_cast<const float *>(addr))
#define pgm_read_ptr(addr) (*reinterpret_cast<const void *const *>(addr))
