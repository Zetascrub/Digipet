#pragma once

// Real Arduino core header, unmodified in spirit: a tiny interface some
// Print overloads accept. Nothing in this project implements it, but
// Print.h's real signature references it, so it needs to exist.
class Printable {
 public:
  virtual size_t printTo(class Print &p) const = 0;
  virtual ~Printable() {}
};
