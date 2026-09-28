#include "ota/portal_stop_deadline.h"

#include <cassert>
#include <cstdint>

int main() {
  PortalStopDeadline deadline;
  assert(!deadline.pending());
  assert(!deadline.due(100));

  deadline.schedule(1000, 250);
  assert(deadline.pending());
  assert(!deadline.due(1249));
  assert(deadline.due(1250));

  deadline.clear();
  assert(!deadline.pending());
  assert(!deadline.due(5000));

  deadline.schedule(UINT32_MAX - 100, 250);
  assert(!deadline.due(100));
  assert(deadline.due(149));
  return 0;
}
