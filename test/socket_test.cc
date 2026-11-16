// DashNet - native unit tests for the TCP socket layer.
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dash/nina_commands.h"
#include "dash/nina_socket.h"
#include "dash/types.h"
#include "sim/fake_nina.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using dashtest::FakeNina;
using Bytes = std::vector<uint8_t>;
using Params = std::vector<std::vector<uint8_t>>;
namespace nina = dash::nina;

struct Fixture {
  FakeNina sim;
  dash::NinaFrame frame;
  dash::NinaSocket socket;

  Fixture() : frame(sim), socket(frame) {
    sim.Begin();
    sim.SetReply(nina::Opcode::kGetSocket, {{0}});
    sim.SetStatusReply(nina::Opcode::kStartClientTcp, nina::kAck);
    sim.SetReply(nina::Opcode::kGetClientStateTcp,
                 {{static_cast<uint8_t>(nina::SocketState::kEstablished)}});
  }

  bool ConnectTcp() {
    return socket.Connect(dash::Ipv4(93, 184, 216, 34), 80, 5000);
  }
};

void TestConnectIpEncoding() {
  Fixture f;
  CHECK(f.ConnectTcp());
  CHECK(f.socket.Handle() == 0);

  const FakeNina::Frame& frame = f.sim.Frames()[1];
  CHECK(frame.cmd == nina::Opcode::kStartClientTcp);
  CHECK(frame.params.size() == 6);
  CHECK(frame.params[0].empty());  // no hostname for a plain IP connect
  // Ipv4 travels as a native little endian uint32_t.
  CHECK(frame.params[1] == Bytes({34, 216, 184, 93}));
  CHECK(frame.params[2] == Bytes({0x00, 0x50}));
  CHECK(frame.params[3] == Bytes({0}));
  CHECK(frame.params[4] == Bytes({static_cast<uint8_t>(nina::ProtMode::kTcp)}));
  CHECK(frame.params[5] == Bytes({0x13, 0x88}));  // 5000 ms
}

void TestConnectTlsEncoding() {
  Fixture f;
  f.sim.SetReply(nina::Opcode::kGetSocket, {{1}});
  CHECK(f.socket.ConnectTls("example.com", 443, 20000));

  const FakeNina::Frame& frame = f.sim.Frames()[1];
  CHECK(frame.cmd == nina::Opcode::kStartClientTcp);
  CHECK(frame.params.size() == 6);
  CHECK(std::string(frame.params[0].begin(), frame.params[0].end()) ==
        "example.com");
  CHECK(frame.params[1] == Bytes({0, 0, 0, 0}));
  CHECK(frame.params[2] == Bytes({0x01, 0xBB}));
  CHECK(frame.params[3] == Bytes({1}));
  CHECK(frame.params[4] == Bytes({static_cast<uint8_t>(nina::ProtMode::kTls)}));
  CHECK(frame.params[5] == Bytes({0x4E, 0x20}));
}

void TestConnectNoSocket() {
  Fixture f;
  f.sim.SetReply(nina::Opcode::kGetSocket, {{0xFF}});
  CHECK(!f.ConnectTcp());
  CHECK(!f.socket.IsOpen());
  CHECK(f.socket.Handle() == dash::NinaSocket::kNoSocket);
}

void TestConnectNotEstablished() {
  FakeNina sim;
  sim.Begin();
  dash::NinaFrame frame(sim);
  dash::NinaSocket socket(frame);
  sim.SetReply(nina::Opcode::kGetSocket, {{0}});
  sim.SetStatusReply(nina::Opcode::kStartClientTcp, nina::kAck);
  sim.SetReply(nina::Opcode::kGetClientStateTcp,
               {{static_cast<uint8_t>(nina::SocketState::kSynSent)}});
  sim.SetStatusReply(nina::Opcode::kStopClientTcp, nina::kAck);

  CHECK(!socket.Connect(dash::Ipv4(1, 2, 3, 4), 80, 300));
  CHECK(!socket.IsOpen());
}

void TestWrite() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetReply(nina::Opcode::kSendDataTcp, {{5, 0}});

  const char payload[] = "hello";
  CHECK(f.socket.Write(payload, 5) == 5);

  const FakeNina::Frame& frame = f.sim.Frames().back();
  CHECK(frame.cmd == nina::Opcode::kSendDataTcp);
  CHECK(frame.params.size() == 2);
  CHECK(frame.params[0] == Bytes({0}));
  CHECK(std::string(frame.params[1].begin(), frame.params[1].end()) == "hello");
}

void TestWritePartial() {
  Fixture f;
  CHECK(f.ConnectTcp());

  // Accept two bytes per command: the driver must keep sending the rest.
  f.sim.SetHandler(nina::Opcode::kSendDataTcp,
                   [](const FakeNina::Frame& frame) {
                     const size_t offered = frame.params[1].size();
                     const uint8_t accepted =
                         offered > 2 ? 2 : static_cast<uint8_t>(offered);
                     return Params{{accepted, 0}};
                   });

  const char payload[] = "abcdef";
  CHECK(f.socket.Write(payload, 6) == 6);
}

void TestRead() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetDataReady(true);
  f.sim.SetReply(nina::Opcode::kAvailDataTcp, {{5, 0}});
  f.sim.SetReply(nina::Opcode::kGetDatabufTcp, {{'H', 'E', 'L', 'L', 'O'}});

  CHECK(f.socket.Available() == 5);
  CHECK(f.socket.Read(nullptr, 0) == 0);

  char buffer[8] = {0};
  CHECK(f.socket.Read(buffer, sizeof(buffer)) == 5);
  CHECK(std::memcmp(buffer, "HELLO", 5) == 0);

  const FakeNina::Frame& frame = f.sim.Frames().back();
  CHECK(frame.cmd == nina::Opcode::kGetDatabufTcp);
  CHECK(frame.params.size() == 2);
  CHECK(frame.params[0] == Bytes({0}));
  // Maximum read size, little endian 16 bit.
  CHECK(frame.params[1] == Bytes({8, 0}));
}

void TestReadBlockedWithoutIrq() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetDataReady(false);
  const size_t before = f.sim.Transactions();

  char buffer[4];
  CHECK(f.socket.Read(buffer, sizeof(buffer)) == 0);
  CHECK(f.socket.Available() == 0);
  CHECK(f.sim.Transactions() == before);
}

void TestPeek() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetDataReady(true);
  f.sim.SetReply(nina::Opcode::kGetDataTcp, {{'X'}});
  CHECK(f.socket.Peek() == 'X');
}

void TestConnectedAndClose() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetDataReady(false);
  CHECK(f.socket.Connected());

  f.sim.SetReply(nina::Opcode::kGetClientStateTcp,
                 {{static_cast<uint8_t>(nina::SocketState::kClosed)}});
  CHECK(!f.socket.Connected());
  CHECK(!f.socket.IsOpen());

  // Closing an already forgotten socket succeeds without a transaction.
  CHECK(f.socket.Close());
}

void TestCloseOpenSocket() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetStatusReply(nina::Opcode::kStopClientTcp, nina::kAck);
  CHECK(f.socket.Close());
  CHECK(!f.socket.IsOpen());
  CHECK(f.sim.Frames().back().cmd == nina::Opcode::kStopClientTcp);
}

void TestRemote() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetReply(nina::Opcode::kGetRemoteData,
                 {{93, 184, 216, 34}, {0x01, 0xBB}});

  dash::Ipv4 ip;
  uint16_t port = 0;
  CHECK(f.socket.Remote(ip, port));
  CHECK(ip == dash::Ipv4(93, 184, 216, 34));
  CHECK(port == 443);
}

void TestWaitDataSent() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetReply(nina::Opcode::kDataSentTcp, {{1}});
  CHECK(f.socket.WaitDataSent(300));
}

void TestDoubleConnectReusesSocket() {
  Fixture f;
  CHECK(f.ConnectTcp());
  f.sim.SetStatusReply(nina::Opcode::kStopClientTcp, nina::kAck);
  CHECK(f.ConnectTcp());  // must stop the previous socket first
  bool saw_stop = false;
  for (size_t i = 0; i < f.sim.Frames().size(); ++i) {
    if (f.sim.Frames()[i].cmd == nina::Opcode::kStopClientTcp) {
      saw_stop = true;
    }
  }
  CHECK(saw_stop);
}

void TestUdpListen() {
  FakeNina sim;
  sim.Begin();
  dash::NinaFrame frame(sim);
  dash::NinaSocket socket(frame);
  sim.SetReply(nina::Opcode::kGetSocket, {{2}});
  sim.SetStatusReply(nina::Opcode::kStartServerTcp, nina::kAck);

  CHECK(socket.Listen(1234, nina::ProtMode::kUdp));
  CHECK(socket.Handle() == 2);

  const FakeNina::Frame& command = sim.Frames()[1];
  CHECK(command.cmd == nina::Opcode::kStartServerTcp);
  CHECK(command.params.size() == 3);
  CHECK(command.params[0] == Bytes({0x04, 0xD2}));
  CHECK(command.params[1] == Bytes({2}));
  CHECK(command.params[2] ==
        Bytes({static_cast<uint8_t>(nina::ProtMode::kUdp)}));
}

void TestUdpSend() {
  FakeNina sim;
  sim.Begin();
  dash::NinaFrame frame(sim);
  dash::NinaSocket socket(frame);
  sim.SetReply(nina::Opcode::kGetSocket, {{3}});
  sim.SetStatusReply(nina::Opcode::kStartClientTcp, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kInsertDatabuf, nina::kAck);
  sim.SetStatusReply(nina::Opcode::kSendDataUdp, nina::kAck);

  CHECK(socket.ConnectUdp(dash::Ipv4(192, 168, 1, 1), 123));
  const FakeNina::Frame& connect_frame = sim.Frames()[1];
  CHECK(connect_frame.cmd == nina::Opcode::kStartClientTcp);
  CHECK(connect_frame.params.size() == 4);
  CHECK(connect_frame.params[0] == Bytes({1, 1, 168, 192}));
  CHECK(connect_frame.params[1] == Bytes({0x00, 0x7B}));
  CHECK(connect_frame.params[2] == Bytes({3}));
  CHECK(connect_frame.params[3] ==
        Bytes({static_cast<uint8_t>(nina::ProtMode::kUdp)}));

  const char payload[] = "ping";
  CHECK(socket.Insert(payload, 4));
  const FakeNina::Frame& insert_frame = sim.Frames().back();
  CHECK(insert_frame.cmd == nina::Opcode::kInsertDatabuf);
  CHECK(insert_frame.params[1] == Bytes({'p', 'i', 'n', 'g'}));

  CHECK(socket.SendUdp());
  CHECK(sim.Frames().back().cmd == nina::Opcode::kSendDataUdp);
}

void TestServerAccept() {
  FakeNina sim;
  sim.Begin();
  dash::NinaFrame frame(sim);
  dash::NinaSocket server(frame);
  sim.SetReply(nina::Opcode::kGetSocket, {{0}});
  sim.SetStatusReply(nina::Opcode::kStartServerTcp, nina::kAck);
  sim.SetReply(nina::Opcode::kAvailDataTcp, {{3}});
  sim.SetStatusReply(nina::Opcode::kStopServerTcp, nina::kAck);
  sim.SetReply(nina::Opcode::kGetStateTcp,
               {{static_cast<uint8_t>(nina::SocketState::kEstablished)}});

  CHECK(server.Listen(80));
  CHECK(server.Accept() == 3);

  const FakeNina::Frame& accept_frame = sim.Frames().back();
  CHECK(accept_frame.cmd == nina::Opcode::kAvailDataTcp);
  CHECK(accept_frame.params.size() == 2);
  CHECK(accept_frame.params[0] == Bytes({0}));
  CHECK(accept_frame.params[1] == Bytes({1}));

  CHECK(server.ServerState() == nina::SocketState::kEstablished);
  CHECK(server.StopServer());
  CHECK(sim.Frames().back().cmd == nina::Opcode::kStopServerTcp);
  CHECK(!server.IsOpen());
}

void TestAdoptSocket() {
  FakeNina sim;
  sim.Begin();
  dash::NinaFrame frame(sim);
  dash::NinaSocket socket(frame);
  sim.SetStatusReply(nina::Opcode::kStopClientTcp, nina::kAck);
  socket.Adopt(4);
  CHECK(socket.Handle() == 4);
  CHECK(socket.IsOpen());
}

}  // namespace

int main() {
  TestConnectIpEncoding();
  TestConnectTlsEncoding();
  TestConnectNoSocket();
  TestConnectNotEstablished();
  TestWrite();
  TestWritePartial();
  TestRead();
  TestReadBlockedWithoutIrq();
  TestPeek();
  TestConnectedAndClose();
  TestCloseOpenSocket();
  TestRemote();
  TestWaitDataSent();
  TestDoubleConnectReusesSocket();
  TestUdpListen();
  TestUdpSend();
  TestServerAccept();
  TestAdoptSocket();

  if (g_failures == 0) {
    std::printf("all socket tests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", g_failures);
  return 1;
}
