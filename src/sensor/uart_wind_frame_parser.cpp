/**
 * @file uart_wind_frame_parser.cpp
 * @brief Transactional parser for the bounded UART wind fallback frame.
 */

#include "sensor/uart_wind_frame_parser.h"

#include <string.h>

#include "sensor/wind_parser_numeric.h"

namespace UartWindFrameParser {

bool parse(const char* line, WindData& data) {
  if (line == nullptr || line[0] == '\0') {
    return false;
  }

  const size_t lineLength = strlen(line);
  if (lineLength >= MAX_LINE_LENGTH || line[lineLength - 1] == ',' ||
      strstr(line, ",,") != nullptr) {
    return false;
  }

  char lineCopy[MAX_LINE_LENGTH];
  memcpy(lineCopy, line, lineLength + 1);

  char* tokens[TOKEN_COUNT] = {};
  size_t tokenCount = 0;
  char* saveptr = nullptr;
  char* token = strtok_r(lineCopy, ",", &saveptr);
  while (token != nullptr && tokenCount < TOKEN_COUNT) {
    tokens[tokenCount++] = token;
    token = strtok_r(nullptr, ",", &saveptr);
  }
  if (token != nullptr || tokenCount != TOKEN_COUNT ||
      strcmp(tokens[0], "#") != 0) {
    return false;
  }

  WindData parsed;
  int integer = 0;
  if (!WindParserNumeric::parseIntInRange(
        tokens[1], WIND_NODE_ID_MIN, WIND_NODE_ID_MAX, integer)) {
    return false;
  }
  parsed.nodeId = static_cast<uint8_t>(integer);

  if (!WindParserNumeric::parseIntInRange(
        tokens[2], WIND_OUTPUT_NOT_READY,
        WIND_OUTPUT_RESTORED_UNKNOWN_LATCH, integer)) {
    return false;
  }
  if (!WindDataContract::projectOutputStatusCode(
        integer, parsed.status, parsed.serviceStatus, parsed.activeCause,
        parsed.ntcReadingStatus, parsed.isValid)) {
    return false;
  }
  parsed.statusProtocolVersion = WIND_STATUS_PROTOCOL_VERSION;

  if (!WindParserNumeric::parseIntInRange(
        tokens[3], WIND_DIR_MIN, WIND_DIR_MAX, integer)) {
    return false;
  }
  parsed.windDirection = static_cast<uint16_t>(integer);

  if (!WindParserNumeric::parseFiniteFloat(tokens[4], parsed.windSpeed) ||
      !WindParserNumeric::parseFiniteFloat(tokens[5], parsed.headingSpeed) ||
      !WindParserNumeric::parseFiniteFloat(tokens[6], parsed.soundSpeed) ||
      !WindParserNumeric::parseFiniteFloat(tokens[7], parsed.temperature)) {
    return false;
  }
  parsed.updateAxisWindSpeedsFromPolar();

  data = parsed;
  return true;
}

}  // namespace UartWindFrameParser
