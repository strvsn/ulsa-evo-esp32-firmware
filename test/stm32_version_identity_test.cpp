#include "stm32_update/stm32_version_identity.h"

#include <cstring>

using namespace stm32_update;

int main() {
  Stm32SemanticVersion version = {};
  if (!parseCanonicalStm32Version("1.0.0", version)) return 1;
  if (packStm32VersionCode(version) != STM32_INITIAL_VERSION_CODE) return 2;
  char text[12] = {};
  if (!formatCanonicalStm32Version(version, text, sizeof(text))) return 3;
  if (std::strcmp(text, "1.0.0") != 0) return 4;
  if (parseCanonicalStm32Version("1.0", version)) return 5;
  if (parseCanonicalStm32Version("01.0.0", version)) return 6;
  if (parseCanonicalStm32Version("256.0.0", version)) return 7;
  if (!decodeStm32VersionCode(0x7E010C00UL, version)) return 8;
  if (!formatCanonicalStm32Version(version, text, sizeof(text))) return 9;
  if (std::strcmp(text, "1.12.0") != 0) return 10;
  if (decodeStm32VersionCode(20260831UL, version)) return 11;
  if (STM32_MIN_REVISION != 1UL) return 15;
  if (STM32_UPDATE_CONTRACT_VERSION != 3U) return 16;
  if (!parseCanonicalStm32Version("1.12.1", version)) return 17;
  if (version.major != 1U || version.minor != 12U || version.patch != 1U)
    return 18;
  if (!parseCanonicalStm32Version("255.255.255", version)) return 19;
  if (packStm32VersionCode(version) != 0x7EFFFFFFUL) return 20;
  if (parseCanonicalStm32Version("1.0.0.0", version)) return 21;
  if (parseCanonicalStm32Version("1.0.0-beta", version)) return 22;
  Stm32SemanticVersion left = {};
  Stm32SemanticVersion right = {};
  if (!parseCanonicalStm32Version("1.9.9", left) ||
      !parseCanonicalStm32Version("1.10.0", right) ||
      compareStm32Versions(left, right) >= 0) return 23;
  if (!parseCanonicalStm32Version("1.12.0", left) ||
      !parseCanonicalStm32Version("2.0.0", right) ||
      compareStm32Versions(left, right) >= 0) return 24;
  return 0;
}
