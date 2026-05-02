/*
 *  udp_sender6.h
 *
 *  IPv6 UDP sender for macOS/Linux (POSIX sockets).
 *  Based on UDPSend6 example by Helmut Hlavacs (2022).
 */

#pragma once

extern "C" {
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

/// Header prepended to every UDP packet.
struct RTHeader {
  double time;
  unsigned long packetnum;
};

class UDPSender6 {

public:
  int sock = -1;
  struct sockaddr_in6 addr;
  unsigned long packetnum = 0;

  UDPSender6() = default;
  ~UDPSender6();

  /// Create socket and resolve the destination IPv6 address.
  void init(const char *address, int port);

  /// Send a buffer as a single UDP6 datagram (with RTHeader prepended).
  /// Returns the number of bytes sent (excluding header), or -1 on error.
  int send(const char *buffer, int len);

  /// Close the socket.
  void closeSock();
};
