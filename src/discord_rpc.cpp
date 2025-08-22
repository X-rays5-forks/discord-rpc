#include "discord_rpc.h"

#include "backoff.h"
#include "discord_register.h"
#include "msg_queue.h"
#include "rpc_connection.h"
#include "serialization.h"

#include <atomic>
#include <chrono>
#include <mutex>

#include <glaze/glaze.hpp>

#ifndef DISCORD_DISABLE_IO_THREAD
#include <condition_variable>
#include <thread>
#endif

constexpr size_t MaxMessageSize{16 * 1024};
constexpr size_t MessageQueueSize{8};
constexpr size_t JoinQueueSize{8};

struct QueuedMessage {
  size_t length;
  char buffer[MaxMessageSize];

  void Copy(const QueuedMessage &other) {
    length = other.length;
    if (length) {
      memcpy(buffer, other.buffer, length);
    }
  }
};

struct User {
  // snowflake (64bit int), turned into a ascii decimal string, at most 20 chars
  // +1 null terminator = 21
  char userId[32];
  // 32 unicode glyphs is max name size => 4 bytes per glyph in the worst case,
  // +1 for null terminator = 129
  char username[344];
  // 4 decimal digits + 1 null terminator = 5
  char discriminator[8];
  // optional 'a_' + md5 hex digest (32 bytes) + null terminator = 35
  char avatar[128];
  // Rounded way up because I'm paranoid about games breaking from future
  // changes in these sizes
};

static RpcConnection *Connection{nullptr};
static DiscordEventHandlers QueuedHandlers{};
static DiscordEventHandlers Handlers{};
static std::atomic_bool WasJustConnected{false};
static std::atomic_bool WasJustDisconnected{false};
static std::atomic_bool GotErrorMessage{false};
static std::atomic_bool WasJoinGame{false};
static std::atomic_bool WasSpectateGame{false};
static std::atomic_bool UpdatePresence{false};
static char JoinGameSecret[256];
static char SpectateGameSecret[256];
static int LastErrorCode{0};
static char LastErrorMessage[256];
static int LastDisconnectErrorCode{0};
static char LastDisconnectErrorMessage[256];
static std::mutex PresenceMutex;
static std::mutex HandlerMutex;
static QueuedMessage QueuedPresence{};
static MsgQueue<QueuedMessage, MessageQueueSize> SendQueue;
static MsgQueue<User, JoinQueueSize> JoinAskQueue;
static User connectedUser;

// We want to auto connect, and retry on failure, but not as fast as possible.
// This does expoential backoff from 0.5 seconds to 1 minute
static Backoff ReconnectTimeMs(500, 60 * 1000);
static auto NextConnect = std::chrono::system_clock::now();
static int Pid{0};
static int Nonce{1};

#ifndef DISCORD_DISABLE_IO_THREAD
static void Discord_UpdateConnection(void);
class IoThreadHolder {
private:
  std::atomic_bool keepRunning{true};
  std::mutex waitForIOMutex;
  std::condition_variable waitForIOActivity;
  std::thread ioThread;

public:
  void Start() {
    keepRunning.store(true);
    ioThread = std::thread([&]() {
      const std::chrono::duration<int64_t, std::milli> maxWait{500LL};
      Discord_UpdateConnection();
      while (keepRunning.load()) {
        std::unique_lock<std::mutex> lock(waitForIOMutex);
        waitForIOActivity.wait_for(lock, maxWait);
        Discord_UpdateConnection();
      }
    });
  }

  void Notify() { waitForIOActivity.notify_all(); }

  void Stop() {
    keepRunning.exchange(false);
    Notify();
    if (ioThread.joinable()) {
      ioThread.join();
    }
  }

  ~IoThreadHolder() { Stop(); }
};
#else
class IoThreadHolder {
public:
  void Start() {}
  void Stop() {}
  void Notify() {}
};
#endif // DISCORD_DISABLE_IO_THREAD
static IoThreadHolder *IoThread{nullptr};

static void UpdateReconnectTime() {
  NextConnect =
      std::chrono::system_clock::now() +
      std::chrono::duration<int64_t, std::milli>{ReconnectTimeMs.nextDelay()};
}

#ifdef DISCORD_DISABLE_IO_THREAD
extern "C" DISCORD_EXPORT void Discord_UpdateConnection(void)
#else
static void Discord_UpdateConnection(void)
#endif
{
  if (!Connection) {
    return;
  }

  if (!Connection->IsOpen()) {
    if (std::chrono::system_clock::now() >= NextConnect) {
      UpdateReconnectTime();
      Connection->Open();
    }
  } else {
    // reads

    for (;;) {
      glz::json_t message;

      if (!Connection->Read(message)) {
        break;
      }

      std::string evtName;
      if (!message["evt"].is_null()) {
        evtName = message["evt"].get<std::string>();
      }
      std::string nonce;
      if (!message["nonce"].is_null()) {
        nonce = std::to_string(message["nonce"].as<std::int32_t>());
      }

      if (!nonce.empty()) {
        // in responses only -- should use to match up response when needed.

        if (!evtName.empty() && strcmp(evtName.c_str(), "ERROR") == 0) {
          LastErrorCode = message["data"]["code"].as<std::int32_t>();
          StringCopy(LastErrorMessage, message["data"]["message"].get_string().c_str());
          GotErrorMessage.store(true);
        }
      } else {
        // should have evt == name of event, optional data
        if (evtName.empty()) {
          continue;
        }

        if (strcmp(evtName.c_str(), "ACTIVITY_JOIN") == 0) {
          if (!message["data"]["secret"].is_null()) {
            StringCopy(JoinGameSecret, message["data"]["secret"].get_string().c_str());
            WasJoinGame.store(true);
          }
        } else if (strcmp(evtName.c_str(), "ACTIVITY_SPECTATE") == 0) {
          if (!message["data"]["secret"].is_null()) {
            StringCopy(JoinGameSecret, message["data"]["secret"].get_string().c_str());
            WasJoinGame.store(true);
          }
        } else if (strcmp(evtName.c_str(), "ACTIVITY_JOIN_REQUEST") == 0) {
          std::string userId;
          if (!message["data"]["user"]["id"].is_null()) {
            userId = message["data"]["user"]["id"].get_string();
          }
          std::string username;
          if (!message["data"]["user"]["username"].is_null()) {
            username = message["data"]["user"]["username"].get_string();
          }
          std::string avatar;
          if (message["data"]["user"]["avatar"].is_null()) {
            avatar = message["data"]["user"]["avatar"].get_string();
          }
          auto joinReq = JoinAskQueue.GetNextAddMessage();
          if (!userId.empty() && !username.empty() && joinReq) {
            StringCopy(joinReq->userId, userId.c_str());
            StringCopy(joinReq->username, username.c_str());
            std::string discriminator;
            if (!message["data"]["user"]["discriminator"].is_null()) {
              discriminator = message["data"]["user"]["discriminator"].get_string();
            }
            if (!discriminator.empty()) {
              StringCopy(joinReq->discriminator, discriminator.c_str());
            }
            if (!avatar.empty()) {
              StringCopy(joinReq->avatar, avatar.c_str());
            } else {
              joinReq->avatar[0] = 0;
            }
            JoinAskQueue.CommitAdd();
          }
        }
      }
    }

    // writes
    if (UpdatePresence.exchange(false) && QueuedPresence.length) {
      QueuedMessage local;
      {
        std::lock_guard guard(PresenceMutex);
        local.Copy(QueuedPresence);
      }
      if (!Connection->Write(local.buffer, local.length)) {
        // if we fail to send, requeue
        std::lock_guard guard(PresenceMutex);
        QueuedPresence.Copy(local);
        UpdatePresence.exchange(true);
      }
    }

    while (SendQueue.HavePendingSends()) {
      auto qmessage = SendQueue.GetNextSendMessage();
      Connection->Write(qmessage->buffer, qmessage->length);
      SendQueue.CommitSend();
    }
  }
}

static void SignalIOActivity() {
  if (IoThread != nullptr) {
    IoThread->Notify();
  }
}

static bool RegisterForEvent(const char *evtName) {
  auto qmessage = SendQueue.GetNextAddMessage();
  if (qmessage) {
    qmessage->length = JsonWriteSubscribeCommand(
        qmessage->buffer, sizeof(qmessage->buffer), Nonce++, evtName);
    SendQueue.CommitAdd();
    SignalIOActivity();
    return true;
  }
  return false;
}

static bool DeregisterForEvent(const char *evtName) {
  auto qmessage = SendQueue.GetNextAddMessage();
  if (qmessage) {
    qmessage->length = JsonWriteUnsubscribeCommand(qmessage->buffer, sizeof(qmessage->buffer), Nonce++, evtName);
    SendQueue.CommitAdd();
    SignalIOActivity();
    return true;
  }
  return false;
}

extern "C" DISCORD_EXPORT void
Discord_Initialize(const char *applicationId, const DiscordEventHandlers *handlers, const int autoRegister, const char *optionalSteamId) {
  IoThread = new (std::nothrow) IoThreadHolder();
  if (IoThread == nullptr) {
    return;
  }

  if (autoRegister) {
    if (optionalSteamId && optionalSteamId[0]) {
      Discord_RegisterSteamGame(applicationId, optionalSteamId);
    } else {
      Discord_Register(applicationId, nullptr);
    }
  }

  Pid = GetProcessId();

  {
    std::lock_guard guard(HandlerMutex);

    if (handlers) {
      QueuedHandlers = *handlers;
    } else {
      QueuedHandlers = {};
    }

    Handlers = {};
  }

  if (Connection) {
    return;
  }

  Connection = RpcConnection::Create(applicationId);
  Connection->onConnect = [](glz::json_t& readyMessage) {
    Discord_UpdateHandlers(&QueuedHandlers);
    if (QueuedPresence.length > 0) {
      UpdatePresence.exchange(true);
      SignalIOActivity();
    }

    std::string userId;
    if (!readyMessage["data"]["user"]["id"].is_null()) {
      userId = readyMessage["data"]["user"]["id"].get_string();
    }
    std::string username;
    if (!readyMessage["data"]["user"]["username"].is_null()) {
      username = readyMessage["data"]["user"]["username"].get_string();
    }
    std::string avatar;
    if (readyMessage["data"]["user"]["avatar"].is_null()) {
      avatar = readyMessage["data"]["user"]["avatar"].get_string();
    }
    if (!userId.empty() && !username.empty()) {
      StringCopy(connectedUser.userId, userId.c_str());
      StringCopy(connectedUser.username, username.c_str());
      std::string discriminator;
      if (!readyMessage["data"]["user"]["discriminator"].is_null()) {
        discriminator = readyMessage["data"]["user"]["discriminator"].get_string();
      }
      if (!discriminator.empty()) {
        StringCopy(connectedUser.discriminator, discriminator.c_str());
      }
      if (!avatar.empty()) {
        StringCopy(connectedUser.avatar, avatar.c_str());
      } else {
        connectedUser.avatar[0] = 0;
      }
    }
    WasJustConnected.exchange(true);
    ReconnectTimeMs.reset();
  };

  Connection->onDisconnect = [](const int err, const char *message) {
    LastDisconnectErrorCode = err;
    StringCopy(LastDisconnectErrorMessage, message);
    WasJustDisconnected.exchange(true);
    UpdateReconnectTime();
  };

  IoThread->Start();
}

extern "C" DISCORD_EXPORT void Discord_Shutdown(void) {
  if (!Connection) {
    return;
  }

  Connection->onConnect = nullptr;
  Connection->onDisconnect = nullptr;
  Handlers = {};
  QueuedPresence.length = 0;
  UpdatePresence.exchange(false);
  if (IoThread != nullptr) {
    IoThread->Stop();
    delete IoThread;
    IoThread = nullptr;
  }

  RpcConnection::Destroy(Connection);
}

extern "C" DISCORD_EXPORT void
Discord_UpdatePresence(const DiscordRichPresence *presence) {
  {
    std::lock_guard guard(PresenceMutex);
    QueuedPresence.length = JsonWriteRichPresenceObj(QueuedPresence.buffer, sizeof(QueuedPresence.buffer), Nonce++, Pid, presence);
    UpdatePresence.exchange(true);
  }
  SignalIOActivity();
}

extern "C" DISCORD_EXPORT void Discord_ClearPresence(void) {
  Discord_UpdatePresence(nullptr);
}

extern "C" DISCORD_EXPORT void Discord_Respond(const char *userId, /* DISCORD_REPLY_ */ int reply) {
  // if we are not connected, let's not batch up stale messages for later
  if (!Connection || !Connection->IsOpen()) {
    return;
  }

  const auto qmessage = SendQueue.GetNextAddMessage();
  if (qmessage) {
    qmessage->length = JsonWriteJoinReply(
        qmessage->buffer, sizeof(qmessage->buffer), userId, reply, Nonce++);
    SendQueue.CommitAdd();
    SignalIOActivity();
  }
}

extern "C" DISCORD_EXPORT void Discord_RunCallbacks(void) {
  // Note on some weirdness: internally we might connect, get other signals,
  // disconnect any number of times inbetween calls here. Externally, we want
  // the sequence to seem sane, so any other signals are book-ended by calls to
  // ready and disconnect.

  if (!Connection) {
    return;
  }

  const bool wasDisconnected = WasJustDisconnected.exchange(false);
  const bool isConnected = Connection->IsOpen();

  if (isConnected) {
    // if we are connected, disconnect cb first
    std::lock_guard guard(HandlerMutex);
    if (wasDisconnected && Handlers.disconnected) {
      Handlers.disconnected(LastDisconnectErrorCode,
                            LastDisconnectErrorMessage);
    }
  }

  if (WasJustConnected.exchange(false)) {
    std::lock_guard guard(HandlerMutex);
    if (Handlers.ready) {
      const DiscordUser du{connectedUser.userId, connectedUser.username, connectedUser.discriminator, connectedUser.avatar};
      Handlers.ready(&du);
    }
  }

  if (GotErrorMessage.exchange(false)) {
    std::lock_guard guard(HandlerMutex);
    if (Handlers.errored) {
      Handlers.errored(LastErrorCode, LastErrorMessage);
    }
  }

  if (WasJoinGame.exchange(false)) {
    std::lock_guard guard(HandlerMutex);
    if (Handlers.joinGame) {
      Handlers.joinGame(JoinGameSecret);
    }
  }

  if (WasSpectateGame.exchange(false)) {
    std::lock_guard guard(HandlerMutex);
    if (Handlers.spectateGame) {
      Handlers.spectateGame(SpectateGameSecret);
    }
  }

  // Right now this batches up any requests and sends them all in a burst; I
  // could imagine a world where the implementer would rather sequentially
  // accept/reject each one before the next invite is sent. I left it this way
  // because I could also imagine wanting to process these all and maybe show
  // them in one common dialog and/or start fetching the avatars in parallel,
  // and if not it should be trivial for the implementer to make a queue
  // themselves.
  while (JoinAskQueue.HavePendingSends()) {
    auto req = JoinAskQueue.GetNextSendMessage();
    {
      std::lock_guard guard(HandlerMutex);
      if (Handlers.joinRequest) {
        DiscordUser du{req->userId, req->username, req->discriminator,
                       req->avatar};
        Handlers.joinRequest(&du);
      }
    }
    JoinAskQueue.CommitSend();
  }

  if (!isConnected) {
    // if we are not connected, disconnect message last
    std::lock_guard guard(HandlerMutex);
    if (wasDisconnected && Handlers.disconnected) {
      Handlers.disconnected(LastDisconnectErrorCode,
                            LastDisconnectErrorMessage);
    }
  }
}

extern "C" DISCORD_EXPORT void
Discord_UpdateHandlers(DiscordEventHandlers *newHandlers) {
  if (newHandlers) {
#define HANDLE_EVENT_REGISTRATION(handler_name, event)                         \
  if (!Handlers.handler_name && newHandlers->handler_name) {                   \
    RegisterForEvent(event);                                                   \
  } else if (Handlers.handler_name && !newHandlers->handler_name) {            \
    DeregisterForEvent(event);                                                 \
  }

    std::lock_guard guard(HandlerMutex);
    HANDLE_EVENT_REGISTRATION(joinGame, "ACTIVITY_JOIN")
    HANDLE_EVENT_REGISTRATION(spectateGame, "ACTIVITY_SPECTATE")
    HANDLE_EVENT_REGISTRATION(joinRequest, "ACTIVITY_JOIN_REQUEST")

#undef HANDLE_EVENT_REGISTRATION

    Handlers = *newHandlers;
  } else {
    std::lock_guard guard(HandlerMutex);
    Handlers = {};
  }
}
