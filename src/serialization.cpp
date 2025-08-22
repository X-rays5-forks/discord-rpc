#include "serialization.h"
#include "connection.h"
#include "discord_rpc.h"

#include <glaze/glaze.hpp>

#pragma warning(push)
#pragma warning(disable : 5246)

size_t JsonWriteRichPresenceObj(char *dest, size_t maxLen, const int nonce, const int pid, const DiscordRichPresence *presence) {
  glz::json_t message;
  message["nonce"] = nonce;
  message["cmd"] = "SET_ACTIVITY";
  message["args"]["pid"] = pid;
  if (presence) {
    message["args"]["activity"]["state"] = presence->state;
    message["args"]["activity"]["details"] = presence->details;

    /** timestamp */
    if (presence->startTimestamp) {
      message["args"]["activity"]["timestamps"]["start"] = presence->startTimestamp;
    }
    if (presence->endTimestamp) {
      message["args"]["activity"]["timestamps"]["end"] = presence->endTimestamp;
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
        message["args"]["activity"]["party"]["size"] = std::array<std::int32_t, 2>{presence->partySize, presence->partyMax};
      }
      if (presence->partyPrivacy) {
        message["args"]["activity"]["party"]["privacy"] = presence->partyPrivacy;
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
  message["v"] = version;
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
  message["nonce"] = nonce;
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
  message["nonce"] = nonce;
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
  message["nonce"] = nonce;
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
