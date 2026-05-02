/*
 *  udp_sender6.cpp
 *
 *  IPv6 UDP sender for macOS/Linux (POSIX sockets).
 *  Based on UDPSend6 example by Helmut Hlavacs (2022).
 */

// Enable RFC 3542 IPv6 socket options (IPV6_DONTFRAG etc.) on macOS
#define __APPLE_USE_RFC_3542

#include "udp_sender6.h"

#include <cstdio>
#include <ctime>
#include <iostream>

UDPSender6::~UDPSender6() {
  if (sock >= 0) {
    ::close(sock);
    sock = -1;
  }
}

void UDPSender6::init(const char *address, int port) {
  // Close any previously opened socket
  if (sock >= 0) {
    ::close(sock);
  }

  sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("UDPSender6: socket creation failed");
    return;
  }

  // Allow the kernel to fragment large IPv6 datagrams at the source.
  // Without this, macOS rejects datagrams exceeding the path MTU (~16KB on
  // loopback) with EMSGSIZE.
  int off = 0;
  setsockopt(sock, IPPROTO_IPV6, IPV6_DONTFRAG, &off, sizeof(off));

  // Enlarge send buffer to accommodate full-size datagrams
  int sndbuf = 256 * 1024;
  setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

  struct addrinfo hints;
  memset(&addr, 0, sizeof(addr));
  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = 0;

  struct addrinfo *result = nullptr;
  int ret = getaddrinfo(address, nullptr, &hints, &result);
  if (ret != 0) {
    std::cerr << "UDPSender6: getaddrinfo failed for \"" << address
              << "\": " << gai_strerror(ret) << std::endl;
    ::close(sock);
    sock = -1;
    return;
  }

  bool found = false;
  for (addrinfo *ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    if (ptr->ai_family == AF_INET6) {
      memcpy(&addr, ptr->ai_addr, ptr->ai_addrlen);
      addr.sin6_port = htons(port);
      addr.sin6_family = AF_INET6;
      found = true;
      break;
    }
  }
  freeaddrinfo(result);

  if (!found) {
    std::cerr << "UDPSender6: no IPv6 address found for \"" << address << "\""
              << std::endl;
    ::close(sock);
    sock = -1;
    return;
  }

  std::cout << "UDPSender6: initialized → [" << address << "]:" << port
            << std::endl;
}

int UDPSender6::send(const char *buffer, int len) {
  if (sock < 0)
    return -1;

  // Max UDP payload ~65507 bytes; we add RTHeader in front.
  static constexpr int kMaxPayload = 65000;
  if (len > kMaxPayload) {
    std::cerr << "UDPSender6: payload too large (" << len << " > "
              << kMaxPayload << ")" << std::endl;
    return 0;
  }

  packetnum++;

  RTHeader header;
  header.time = clock() / (double)CLOCKS_PER_SEC;
  header.packetnum = packetnum;

  // Build the datagram: [RTHeader | payload]
  char sendbuffer[65000 + sizeof(RTHeader)];
  memcpy(sendbuffer, &header, sizeof(header));
  memcpy(sendbuffer + sizeof(header), buffer, len);

  ssize_t ret = sendto(sock, sendbuffer, sizeof(header) + len, 0,
                       (const struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    perror("UDPSender6: sendto failed");
    return -1;
  }

  return static_cast<int>(ret - sizeof(header));
}

void UDPSender6::closeSock() {
  if (sock >= 0) {
    ::close(sock);
    sock = -1;
  }
}
