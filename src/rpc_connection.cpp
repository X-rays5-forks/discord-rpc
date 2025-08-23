#include "serialization.h"
#include "connection.h"
#include "discord_rpc.h"

#pragma warning(push)
#pragma warning(disable : 4800)
#pragma warning(disable : 5045)
#include <glaze/glaze.hpp>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 5246)

size_t JsonWriteRichPresenceObj(char *dest, const size_t maxLen, const int nonce, const int pid, const DiscordRichPresence *presence) {
  glz::json_t message;
  message["nonce"] = (double)nonce;
  message["cmd"] = "SET_ACTIVITY";
  message["args"]["pid"] = (double)pid;
  if (presence) {
    message["args"]["activity"]["state"] = presence->state;
    message["args"]["activity"]["details"] = presence->details;

    /** timestamp */
    if (presence->startTimestamp) {
      message["args"]["activity"]["timestamps"]["start"] = (double)presence->startTimestamp;
    }
    if (presence->endTimestamp) {
      message["args"]["activity"]["timestamps"]["end"] = (double)presence->endTimestamp;
    }

    /** assets */
    if (presence->largeImageKey) {
      message["args"]["activity"]["assets"]["large_image"] = presence->largeImageKey;
    }
    if (presence->largeImageText) {
      message["args"]["activity"]["assets"]["large_text"] = presence->largeImageText;
    }
    if (presence->smallImageKey) {
      message["args"]["activity"]["assets"]["small_image"] = presence->smallImageKey;
    }
    if (presence->smallImageText) {
      message["args"]["activity"]["assets"]["small_text"] = presence->smallImageText;
    }

    /** party */
    if (presence->partyId) {
      message["args"]["activity"]["party"]["id"] = presence->partyId;
      if (presence->partySize && presence->partyMax) {
        message["args"]["activity"]["party"]["size"] = glz::json_t::array_t{(double)presence->partySize, (double)presence->partyMax};
      }
      if (presence->partyPrivacy) {
        message["args"]["activity"]["party"]["privacy"] = (double)presence->partyPrivacy;
      }
    }

    if (presence->matchSecret && presence->joinSecret && presence->spectateSecret) {
      message["args"]["activity"]["secrets"]["match"] = presence->matchSecret;
      message["args"]["activity"]["secrets"]["join"] = presence->joinSecret;
      message["args"]["activity"]["secrets"]["spectate"] = presence->spectateSecret;
    }

    message["args"]["activity"]["instance"] = presence->instance != 0;
  }

  std::string buff{};
  const auto ec = glz::write_json(message, buff);
  assert(!ec);

  size_t len = buff.size();
  if (len >= maxLen) {
    len = maxLen - 1; // leave space for null terminator
  }

  std::memcpy(dest, buff.data(), len);
  dest[len] = '\0';

  return len;
}

size_t JsonWriteHandshakeObj(char *dest, const size_t maxLen, int version, const char *applicationId) {
  glz::json_t message;
  message["v"] = (double)version;
  message["client_id"] = applicationId;

  std::string buff{};
  const auto ec = glz::write_json(message, buff);
  assert(!ec);

  size_t len = buff.size();
  if (len >= maxLen) {
    len = maxLen - 1; // leave space for null terminator
  }

  std::memcpy(dest, buff.data(), len);
  dest[len] = '\0';

  return len;
}

size_t JsonWriteSubscribeCommand(char *dest, size_t maxLen, int nonce, const char *evtName) {
  glz::json_t message;
  message["nonce"] = (double)nonce;
  message["cmd"] = "SUBSCRIBE";
  message["evt"] = evtName;

  std::string buff{};
  const auto ec = glz::write_json(message, buff);
  assert(!ec);

  size_t len = buff.size();
  if (len >= maxLen) {
    len = maxLen - 1;
  }

  std::memcpy(dest, buff.data(), len);
  dest[len] = '\0';

  return len;
}

size_t JsonWriteUnsubscribeCommand(char *dest, const size_t maxLen, int nonce, const char *evtName) {
  glz::json_t message;
  message["nonce"] = (double)nonce;
  message["cmd"] = "UNSUBSCRIBE";
  message["evt"] = evtName;

  std::string buff{};
  const auto ec = glz::write_json(message, buff);
  assert(!ec);

  size_t len = buff.size();
  if (len >= maxLen) {
    len = maxLen - 1;
  }

  std::memcpy(dest, buff.data(), len);
  dest[len] = '\0';

  return len;
}

size_t JsonWriteJoinReply(char *dest, const size_t maxLen, const char *userId, const int reply, const int nonce) {
  glz::json_t message;
  message["cmd"] = reply == DISCORD_REPLY_YES ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_JOIN_REQUEST";
  message["nonce"] = (double)nonce;
  message["args"]["user_id"] = userId;

  std::string buff{};
  const auto ec = glz::write_json(message, buff);
  assert(!ec);

  size_t len = buff.size();
  if (len >= maxLen) {
    len = maxLen - 1;
  }

  std::memcpy(dest, buff.data(), len);
  dest[len] = '\0';

  return len;
}

#pragma warning(pop)
