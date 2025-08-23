#include "rpc_connection.h"
#include "serialization.h"

#include <glaze/glaze.hpp>
#include <utility>

static constexpr int RpcVersion = 1;
static RpcConnection Instance;

/*static*/ RpcConnection *RpcConnection::Create(const char *applicationId) {
  Instance.connection = BaseConnection::Create();
  StringCopy(Instance.appId, applicationId);
  return &Instance;
}

/*static*/ void RpcConnection::Destroy(RpcConnection *&c) {
  c->Close();
  BaseConnection::Destroy(c->connection);
  c = nullptr;
}

void RpcConnection::Open() {
  if (state == State::Connected) {
    return;
  }

  if (state == State::Disconnected && !connection->Open()) {
    return;
  }

  if (state == State::SentHandshake) {
    glz::json_t message;
    if (Read(message)) {
      std::string cmd;
      if (!message["cmd"].is_null()) {
        cmd = message["cmd"].get_string();
      }
      std::string evt;
      if (!message["evt"].is_null()) {
        evt = message["evt"].get_string();
      }
      if (!cmd.empty() && !evt.empty() && !strcmp(cmd.c_str(), "DISPATCH") && !strcmp(evt.c_str(), "READY")) {
        state = State::Connected;
        if (onConnect) {
          onConnect(message);
        }
      }
    }
  } else {
    sendFrame.opcode = Opcode::Handshake;
    sendFrame.length = static_cast<uint32_t>(JsonWriteHandshakeObj(sendFrame.message, sizeof(sendFrame.message), RpcVersion, appId));

    if (connection->Write(&sendFrame, sizeof(MessageFrameHeader) + sendFrame.length)) {
      state = State::SentHandshake;
    } else {
      Close();
    }
  }
}

void RpcConnection::Close() {
  if (onDisconnect &&
      (state == State::Connected || state == State::SentHandshake)) {
    onDisconnect(lastErrorCode, lastErrorMessage);
  }
  connection->Close();
  state = State::Disconnected;
}

bool RpcConnection::Write(const void *data, const size_t length) {
  sendFrame.opcode = Opcode::Frame;
  memcpy(sendFrame.message, data, length);
  sendFrame.length = static_cast<uint32_t>(length);
  if (!connection->Write(&sendFrame, sizeof(MessageFrameHeader) + length)) {
    Close();
    return false;
  }
  return true;
}

bool RpcConnection::Read(glz::json_t& message) {
  if (state != State::Connected && state != State::SentHandshake) {
    return false;
  }
  MessageFrame readFrame;
  for (;;) {
    bool didRead = connection->Read(&readFrame, sizeof(MessageFrameHeader));
    if (!didRead) {
      if (!connection->isOpen) {
        lastErrorCode = std::to_underlying(ErrorCode::PipeClosed);
        StringCopy(lastErrorMessage, "Pipe closed");
        Close();
      }
      return false;
    }

    if (readFrame.length > 0) {
      didRead = connection->Read(readFrame.message, readFrame.length);
      if (!didRead) {
        lastErrorCode = std::to_underlying(ErrorCode::ReadCorrupt);
        StringCopy(lastErrorMessage, "Partial data in frame");
        Close();
        return false;
      }
      readFrame.message[readFrame.length] = 0;
    }

    glz::error_ctx ec;
    switch (readFrame.opcode) {
    case Opcode::Close: {
      ec = glz::read_json(message, readFrame.message);
      assert(!ec);
      lastErrorCode = message["code"].as<std::int32_t>();
      StringCopy(lastErrorMessage, message["message"].get_string().c_str());
      Close();
      return false;
    }
    case Opcode::Frame:
      ec = glz::read_json(message, readFrame.message);
      assert(!ec);
      return true;
    case Opcode::Ping:
      readFrame.opcode = Opcode::Pong;
      if (!connection->Write(&readFrame, sizeof(MessageFrameHeader) + readFrame.length)) {
        Close();
      }
      break;
    case Opcode::Pong:
      break;
    case Opcode::Handshake:
    default:
      // something bad happened
      lastErrorCode = std::to_underlying(ErrorCode::ReadCorrupt);
      StringCopy(lastErrorMessage, "Bad ipc frame");
      Close();
      return false;
    }
  }
}