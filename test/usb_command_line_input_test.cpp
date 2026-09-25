#include <assert.h>
#include <string.h>

#include "commands/usb_command_line_input.h"

template <typename Input>
static UsbCommandLineEvent feedRepeated(
    Input& input,
    char value,
    size_t count) {
  UsbCommandLineEvent event = UsbCommandLineEvent::None;
  for (size_t i = 0; i < count; ++i) {
    event = input.feed(value);
  }
  return event;
}

int main() {
  {
    UsbCommandLineInput input;
    assert(feedRepeated(input, 'a', UsbCommandLineInput::MAX_LENGTH - 1) ==
           UsbCommandLineEvent::CharacterAppended);
    assert(input.length() == UsbCommandLineInput::MAX_LENGTH - 1);
    assert(input.feed('\n') == UsbCommandLineEvent::LineReady);
    input.clearLine();
  }

  {
    UsbCommandLineInput input;
    assert(feedRepeated(input, 'b', UsbCommandLineInput::MAX_LENGTH) ==
           UsbCommandLineEvent::CharacterAppended);
    assert(input.length() == UsbCommandLineInput::MAX_LENGTH);
    assert(input.feed('\r') == UsbCommandLineEvent::LineReady);
    input.clearLine();
    assert(input.feed('\n') == UsbCommandLineEvent::None);
  }

  {
    UsbCommandLineInput input;
    feedRepeated(input, 'c', UsbCommandLineInput::MAX_LENGTH);
    assert(input.feed('x') == UsbCommandLineEvent::None);
    assert(input.isDiscarding());
    assert(input.length() == 0);
    feedRepeated(input, 'z', 8192);
    assert(input.isDiscarding());
    assert(input.length() == 0);
    assert(input.feed('\b') == UsbCommandLineEvent::None);
    assert(input.feed(0x7f) == UsbCommandLineEvent::None);
    assert(input.feed('y') == UsbCommandLineEvent::None);
    assert(input.feed('\r') == UsbCommandLineEvent::LineRejected);
    assert(input.feed('\n') == UsbCommandLineEvent::None);
    assert(!input.isDiscarding());
    assert(input.length() == 0);

    assert(input.feed('h') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('e') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('l') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('p') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('\n') == UsbCommandLineEvent::LineReady);
    assert(strcmp(input.line(), "help") == 0);
  }

  {
    UsbCommandLineInput input;
    assert(input.feed('a') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('b') == UsbCommandLineEvent::CharacterAppended);
    assert(input.feed('\b') == UsbCommandLineEvent::CharacterRemoved);
    assert(strcmp(input.line(), "a") == 0);
    assert(input.feed(0x01) == UsbCommandLineEvent::None);
    assert(strcmp(input.line(), "a") == 0);
  }

  {
    UsbCommandLineInput input;
    feedRepeated(input, 'q', UsbCommandLineInput::MAX_LENGTH);
    assert(input.feed(0x7f) == UsbCommandLineEvent::CharacterRemoved);
    assert(input.length() == UsbCommandLineInput::MAX_LENGTH - 1);
    assert(input.feed('r') == UsbCommandLineEvent::CharacterAppended);
    assert(input.length() == UsbCommandLineInput::MAX_LENGTH);
    assert(input.feed('\n') == UsbCommandLineEvent::LineReady);
  }

  return 0;
}
