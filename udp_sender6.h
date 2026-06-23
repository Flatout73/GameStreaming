/*
 *  udp_sender6.h
 *
 *  IPv6 UDP sender for macOS/Linux (POSIX sockets).
 */

#pragma once

extern "C" {
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <cstdint>

/// Header prepended to every UDP packet. The client derives its payload offset
/// from sizeof(RTHeader), so the width is pinned (not `unsigned long`, which is
/// 4 bytes on LLP64/ILP32) and asserted to keep both sides in lockstep.
struct RTHeader {
  double time;
  uint64_t packetnum;
};

static_assert(sizeof(RTHeader) == 16, "RTHeader wire layout must be 16 bytes");

/// Report sent back from the receiver every ~10 s.
/// Layout matches the Swift ReceiverReport (3 x double, 24 bytes).
struct ReceiverReport {
  double receivedByteRate;
  double packetLossRate;
  double frameRate;
};

class UDPSender6 {

public:
  int sock = -1;
  struct sockaddr_in6 addr;
  uint64_t packetnum = 0;

  UDPSender6() = default;
  ~UDPSender6();

  /// Create socket and resolve the destination IPv6 address.
  void init(const char *address, int port);

  /// Send a buffer as a single UDP6 datagram (with RTHeader prepended).
  /// Returns the number of bytes sent (excluding header), or -1 on error.
  int send(const char *buffer, int len);

  /// Non-blocking poll for an incoming receiver report on this socket.
  /// Returns true and fills `outReport` if a well-formed report was read.
  /// Returns false otherwise (no data, or unexpected size).
  bool pollReceiverReport(ReceiverReport &outReport);

  void closeSock();
};
