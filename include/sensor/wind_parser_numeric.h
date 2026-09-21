#ifndef ULSA_WIND_PARSER_NUMERIC_H
#define ULSA_WIND_PARSER_NUMERIC_H

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

namespace WindParserNumeric {

inline bool parseIntInRange(
    const char* text,
    int minimum,
    int maximum,
    int& result) {
  if (text == nullptr || text[0] == '\0' || minimum > maximum) {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(text, &end, 10);
  if (end == text || *end != '\0' || errno == ERANGE ||
      parsed < INT_MIN || parsed > INT_MAX ||
      parsed < minimum || parsed > maximum) {
    return false;
  }

  result = static_cast<int>(parsed);
  return true;
}

inline bool parseFiniteFloat(const char* text, float& result) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE || !isfinite(parsed)) {
    return false;
  }

  result = parsed;
  return true;
}

}  // namespace WindParserNumeric

#endif  // ULSA_WIND_PARSER_NUMERIC_H
