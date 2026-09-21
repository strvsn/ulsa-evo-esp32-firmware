#ifndef ULSA_USB_COMMAND_LINE_INPUT_H
#define ULSA_USB_COMMAND_LINE_INPUT_H

#include <stddef.h>
#include <stdint.h>

enum class UsbCommandLineEvent : uint8_t {
  None = 0,
  CharacterAppended,
  CharacterRemoved,
  LineReady,
  EmptyLine,
  LineRejected,
};

/**
 * Fixed-capacity USB-CDC command line input shared by Demo and Initial.
 *
 * Once a printable byte exceeds MAX_LENGTH, the partial line is cleared and
 * all input is discarded through the next CR or LF. Backspace cannot turn an
 * already rejected line back into an executable command.
 */
template <size_t MaxLength>
class BoundedAsciiLineInput {
public:
  static_assert(MaxLength > 0, "Line input capacity must be positive");
  static constexpr size_t MAX_LENGTH = MaxLength;

  BoundedAsciiLineInput()
    : _length(0)
    , _discarding(false)
    , _ignoreNextLf(false)
    , _buffer{} {
  }

  UsbCommandLineEvent feed(char value) {
    if (_ignoreNextLf) {
      _ignoreNextLf = false;
      if (value == '\n') {
        return UsbCommandLineEvent::None;
      }
    }

    if (value == '\r' || value == '\n') {
      _ignoreNextLf = value == '\r';
      if (_discarding) {
        _discarding = false;
        clearLine();
        return UsbCommandLineEvent::LineRejected;
      }
      _buffer[_length] = '\0';
      return _length > 0
        ? UsbCommandLineEvent::LineReady
        : UsbCommandLineEvent::EmptyLine;
    }

    if (_discarding) {
      return UsbCommandLineEvent::None;
    }

    if (value == '\b' || value == 0x7f) {
      if (_length == 0) {
        return UsbCommandLineEvent::None;
      }
      --_length;
      _buffer[_length] = '\0';
      return UsbCommandLineEvent::CharacterRemoved;
    }

    if (value < 32 || value >= 127) {
      return UsbCommandLineEvent::None;
    }

    if (_length >= MAX_LENGTH) {
      _discarding = true;
      clearLine();
      return UsbCommandLineEvent::None;
    }

    _buffer[_length++] = value;
    _buffer[_length] = '\0';
    return UsbCommandLineEvent::CharacterAppended;
  }

  const char* line() const {
    return _buffer;
  }

  size_t length() const {
    return _length;
  }

  bool isDiscarding() const {
    return _discarding;
  }

  void clearLine() {
    _length = 0;
    _buffer[0] = '\0';
  }

private:
  size_t _length;
  bool _discarding;
  bool _ignoreNextLf;
  char _buffer[MAX_LENGTH + 1];
};

using UsbCommandLineInput = BoundedAsciiLineInput<256>;

#endif  // ULSA_USB_COMMAND_LINE_INPUT_H
