// winsock2.h (fakewin) — solo las API de socket que usan los enlaces
// UDP del plugin (InstanceLink.h: baliza 41046; ExtLink.h: enlace móvil
// 41047), con las firmas REALES del SDK de Windows, para syntax-check del
// camino #if _WIN32 en Linux. Si una llamada no compila contra estas firmas,
// no compila contra el SDK real.
//
// Nota de tipos: el SDK real usa `int*` en recvfrom/sendto para el
// sockaddrlen (no socklen_t) — las firmas de abajo son exactamente esas.
#pragma once
#include <stdint.h>

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int SOCKET;
typedef unsigned long u_long;
typedef unsigned short u_short;
typedef uint32_t u_int32;
typedef uint16_t u_int16;

#define MAKEWORD(a, b) (WORD)(((a)&0xff) | (((b)&0xff) << 8))
#define MAKELONG(a, b) (DWORD)(((a)&0xffff) | (((DWORD)(b)&0xffff) << 16))

typedef struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    uint32_t iMaxSockets;
    uint32_t iMaxUdpDg;
    WORD lpVendorInfo;
    WORD wCSPVersion;
    DWORD iReserved1;
    DWORD iReserved2;
    char szComments[257];
} WSADATA, *LPWSADATA;

int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WSACleanup(void);

typedef struct in_addr {
    uint32_t S_addr;
} in_addr;

typedef struct sockaddr {
    uint8_t sa_len;
    uint8_t sa_family;
    char sa_data[14];
} sockaddr;

typedef struct sockaddr_in {
    uint8_t sin_len;
    uint16_t sin_family;
    uint16_t sin_port;
    in_addr sin_addr;
    char sin_zero[8];
} sockaddr_in;

typedef int socklen_t;

#define AF_INET 2
#define SOCK_DGRAM 2
#define SOL_SOCKET 0xffff
#define SO_RCVTIMEO 0x1005
#define SO_REUSEADDR 0x0004
#define SO_BROADCAST 0x0020
#define INADDR_ANY 0x00000000
#define INADDR_BROADCAST 0xffffffff
#define INADDR_LOOPBACK 0x7f000001

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

SOCKET socket(int af, int type, int protocol);
int closesocket(SOCKET s);
int setsockopt(SOCKET s, int level, int optname, const char* optval, int optlen);
int bind(SOCKET s, const sockaddr* name, int namelen);
int recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* src_addr, int* addrlen);
int sendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen);
