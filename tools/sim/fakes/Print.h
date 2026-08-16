#pragma once

// Minimal stand-in for the Arduino core's Print, header-only (the real one
// splits print()/println() overloads into Print.cpp; here they're all
// implemented in terms of write(), same as upstream, just inline so there's
// no separate .cpp to compile into this tool).

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
 public:
  Print() = default;
  virtual ~Print() {}

  virtual size_t write(uint8_t) = 0;
  size_t write(const char *str) {
    if (!str) return 0;
    return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
  }
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; ++i) n += write(buffer[i]);
    return n;
  }
  size_t write(const char *buffer, size_t size) {
    return write(reinterpret_cast<const uint8_t *>(buffer), size);
  }

  size_t print(const String &s) { return write(s.c_str()); }
  size_t print(const char str[]) { return write(str); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int v, int base = DEC) { return print(static_cast<long>(v), base); }
  size_t print(unsigned int v, int base = DEC) {
    return print(static_cast<unsigned long>(v), base);
  }
  size_t print(long v, int base = DEC) {
    char buf[32];
    formatInt(buf, sizeof(buf), v, base);
    return write(buf);
  }
  size_t print(unsigned long v, int base = DEC) {
    char buf[32];
    formatUInt(buf, sizeof(buf), v, base);
    return write(buf);
  }
  size_t print(long long v, int base = DEC) {
    char buf[32];
    snprintf(buf, sizeof(buf), base == HEX ? "%llx" : "%lld", v);
    return write(buf);
  }
  size_t print(unsigned long long v, int base = DEC) {
    char buf[32];
    snprintf(buf, sizeof(buf), base == HEX ? "%llx" : "%llu", v);
    return write(buf);
  }
  size_t print(double v, int decimals = 2) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return write(buf);
  }

  size_t println(const String &s) {
    size_t n = print(s);
    return n + println();
  }
  size_t println(const char str[]) {
    size_t n = print(str);
    return n + println();
  }
  size_t println(char c) {
    size_t n = print(c);
    return n + println();
  }
  size_t println(int v, int base = DEC) {
    size_t n = print(v, base);
    return n + println();
  }
  size_t println(unsigned int v, int base = DEC) {
    size_t n = print(v, base);
    return n + println();
  }
  size_t println(long v, int base = DEC) {
    size_t n = print(v, base);
    return n + println();
  }
  size_t println(unsigned long v, int base = DEC) {
    size_t n = print(v, base);
    return n + println();
  }
  size_t println(double v, int decimals = 2) {
    size_t n = print(v, decimals);
    return n + println();
  }
  size_t println() { return write("\r\n"); }

  size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    char buf[256];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return written > 0 ? write(buf) : 0;
  }

  virtual void flush() {}

 private:
  static void formatInt(char *buf, size_t bufSize, long v, int base) {
    snprintf(buf, bufSize, base == HEX ? "%lx" : "%ld", v);
  }
  static void formatUInt(char *buf, size_t bufSize, unsigned long v, int base) {
    snprintf(buf, bufSize, base == HEX ? "%lx" : "%lu", v);
  }
};
