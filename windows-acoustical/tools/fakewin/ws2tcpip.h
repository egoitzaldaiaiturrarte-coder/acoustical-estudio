// ws2tcpip.h (fakewin) — acompañante de winsock2.h: el plugin solo usa
// sockaddr_in/AF_INET/INADDR_* del winsock2.h (ver ahí), así que este
// cabecera existe para que el #include de InstanceLink.h/ExtLink.h compile
// en el syntax-check de Linux.
#pragma once
#include <winsock2.h>
