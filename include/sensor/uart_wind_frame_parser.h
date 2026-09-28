#ifndef ULSA_UART_WIND_FRAME_PARSER_H
#define ULSA_UART_WIND_FRAME_PARSER_H

#include <stddef.h>

#include "sensor/wind_data.h"

namespace UartWindFrameParser {

static constexpr size_t MAX_LINE_LENGTH = 128;
static constexpr size_t TOKEN_COUNT = 8;

bool parse(const char* line, WindData& data);

}  // namespace UartWindFrameParser

#endif  // ULSA_UART_WIND_FRAME_PARSER_H
