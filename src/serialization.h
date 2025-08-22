#pragma once

#include <cstdint>

#ifndef __MINGW32__
#pragma warning(push)

#pragma warning(                                                               \
    disable : 4061) // enum is not explicitly handled by a case label
#pragma warning(disable : 4365) // signed/unsigned mismatch
#pragma warning(disable : 4464) // relative include path contains
#pragma warning(disable : 4668) // is not defined as a preprocessor macro
#pragma warning(disable : 6313) // Incorrect operator
#endif                          // __MINGW32__

#ifndef __MINGW32__
#pragma warning(pop)
#endif // __MINGW32__

// if only there was a standard library function for this
template <size_t Len> size_t StringCopy(char (&dest)[Len], const char *src) {
  if (!src || !Len) {
    return 0;
  }
  size_t copied;
  char *out = dest;
  for (copied = 1; *src && copied < Len; ++copied) {
    *out++ = *src++;
  }
  *out = 0;
  return copied - 1;
}

size_t JsonWriteHandshakeObj(char *dest, size_t maxLen, int version,
                             const char *applicationId);

// Commands
struct DiscordRichPresence;
size_t JsonWriteRichPresenceObj(char *dest, size_t maxLen, int nonce, int pid,
                                const DiscordRichPresence *presence);
size_t JsonWriteSubscribeCommand(char *dest, size_t maxLen, int nonce,
                                 const char *evtName);
size_t JsonWriteUnsubscribeCommand(char *dest, size_t maxLen, int nonce,
                                   const char *evtName);
size_t JsonWriteJoinReply(char *dest, size_t maxLen, const char *userId,
                          int reply, int nonce);