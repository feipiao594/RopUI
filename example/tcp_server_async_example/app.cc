#include <algorithm>
#include <atomic>
#include <cstdint>
#include <log.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app.h"

struct Client {
  uint64_t id{0};
  std::shared_ptr<asyncnet::AsyncTcpStream> stream;
  asyncnet::AsyncQueue<std::string> outbox;
};

struct RoomCommand {
  enum class Kind { Join, Leave, Broadcast };
  Kind kind;
  std::shared_ptr<Client> client;
  uint64_t id{0};
  std::string msg;
};

class ChatRoomActor {
public:
  explicit ChatRoomActor(asyncnet::Executor &exec) : exec_(exec) {
    cmds_.bind(exec_);
  }

  void join(std::shared_ptr<Client> client) {
    cmds_.push(RoomCommand{RoomCommand::Kind::Join, std::move(client)});
  }
  void leave(uint64_t id) {
    RoomCommand c;
    c.kind = RoomCommand::Kind::Leave;
    c.id = id;
    cmds_.push(std::move(c));
  }
  void broadcast(std::string msg) {
    RoomCommand c;
    c.kind = RoomCommand::Kind::Broadcast;
    c.msg = std::move(msg);
    cmds_.push(std::move(c));
  }

  asyncnet::Task<void> run() {
    for (;;) {
      auto cmd = co_await cmds_.pop();
      if (!cmd.has_value())
        co_return;
      handle(*cmd);
    }
  }

private:
  void handle(const RoomCommand &cmd) {
    switch (cmd.kind) {
    case RoomCommand::Kind::Join: {
      clients_.push_back(cmd.client);
      broadcastImpl("[server] user#" + std::to_string(cmd.client->id) +
                    " joined (" + std::to_string(clients_.size()) +
                    " online)\n");
      break;
    }
    case RoomCommand::Kind::Leave: {
      clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                    [&](const std::weak_ptr<Client> &w) {
                                      auto c = w.lock();
                                      return !c || c->id == cmd.id;
                                    }),
                     clients_.end());
      broadcastImpl("[server] user#" + std::to_string(cmd.id) + " left (" +
                    std::to_string(clients_.size()) + " online)\n");
      break;
    }
    case RoomCommand::Kind::Broadcast:
      broadcastImpl(cmd.msg);
      break;
    }
  }

  void broadcastImpl(const std::string &msg) {
    for (auto it = clients_.begin(); it != clients_.end();) {
      auto c = it->lock();
      if (!c) {
        it = clients_.erase(it);
        continue;
      }
      c->outbox.push(msg);
      ++it;
    }
  }

private:
  asyncnet::Executor &exec_;
  asyncnet::AsyncQueue<RoomCommand> cmds_;
  std::vector<std::weak_ptr<Client>> clients_;
};

static std::vector<std::string> splitLines(std::string &buffer) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i < buffer.size(); ++i) {
    if (buffer[i] == '\n') {
      size_t end = i;
      if (end > start && buffer[end - 1] == '\r') {
        end -= 1;
      }
      // Copy out before mutating `buffer` to avoid dangling views.
      lines.emplace_back(buffer.substr(start, end - start));
      start = i + 1;
    }
  }
  buffer.erase(0, start);
  return lines;
}

static asyncnet::Task<void> writer(std::shared_ptr<Client> client) {
  for (;;) {
    auto msg = co_await client->outbox.pop();
    if (!msg.has_value())
      co_return;
    co_await asyncnet::sendAll(client->stream, *msg);
  }
}

static asyncnet::Task<void> chatSession(asyncnet::Executor &exec,
                                        std::shared_ptr<Client> client,
                                        std::shared_ptr<ChatRoomActor> room) {
  client->outbox.bind(exec);

  asyncnet::TaskGroup local = asyncnet::taskGroup(exec);
  local.spawn(writer(client));
  room->join(client);

  std::string buffer;
  bool done = false;
  for (;;) {
    if (done)
      break;
    auto chunk = co_await asyncnet::recvSome(client->stream);
    if (!chunk.has_value())
      break;
    buffer.append(*chunk);

    for (auto line : splitLines(buffer)) {
      if (line == "/quit") {
        // Avoid concurrent sendAll() calls; unify all outgoing messages via
        // outbox/writer.
        client->outbox.push("[server] bye\n");
        buffer.clear();
        done = true;
        break;
      }
      std::string msg =
          "user#" + std::to_string(client->id) + ": " + line + "\n";
      room->broadcast(std::move(msg));
    }
  }

  room->leave(client->id);

  client->outbox.close();
  co_await local.join();
  co_return;
}

rophive_async_main async_main(
    asyncnet::Executor &accept_exec, ::RopHive::Hive &hive, int worker_n,
    std::shared_ptr<std::vector<std::shared_ptr<asyncnet::Executor>>> execs) {
  auto room = std::make_shared<ChatRoomActor>(accept_exec);
  asyncnet::spawn(accept_exec, room->run());

  auto next_id = std::make_shared<std::atomic<uint64_t>>(0);

  auto bind_ep = ::RopHive::Network::parseIpEndpoint("0.0.0.0:8080").value();
  co_await asyncnet::serveRoundRobin(
      accept_exec, hive, worker_n, std::move(execs), bind_ep,
      [room, next_id](asyncnet::Executor &exec,
                      std::shared_ptr<asyncnet::AsyncTcpStream> stream) {
        auto client = std::make_shared<Client>();
        client->id = next_id->fetch_add(1, std::memory_order_relaxed) + 1;
        client->stream = std::move(stream);
        return chatSession(exec, std::move(client), room);
      });
  co_return;
}
