#ifndef ULSA_FIRMWARE_PROFILE_H
#define ULSA_FIRMWARE_PROFILE_H

#if (defined(ULSA_PROFILE_DEMO) + defined(ULSA_PROFILE_INITIAL)) != 1
#error "Select exactly one ULSA firmware profile"
#endif

#if defined(ULSA_PROFILE_DEMO)
#define ULSA_PROFILE_IS_DEMO 1
#define ULSA_PROFILE_IS_INITIAL 0
#define ULSA_PROFILE_NAME "demo"
#else
#define ULSA_PROFILE_IS_DEMO 0
#define ULSA_PROFILE_IS_INITIAL 1
#define ULSA_PROFILE_NAME "initial"
#endif

#endif
