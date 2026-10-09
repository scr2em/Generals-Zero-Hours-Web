/*
**	Command & Conquer Generals Zero Hour(tm)
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
** WebAssembly port: Winsock on top of the POSIX socket API.
**
** The BSD socket functions are used directly. The Winsock additions are
** thin: SOCKET is an int, the WSAE* error codes are the errno values (so
** WSAGetLastError is errno, as in the GameSpy SDK's POSIX platform layer), and
** closesocket/ioctlsocket are close/ioctl. Everything that the GameSpy SDK
** headers define as well is defined identically here, so both can be
** included in either order.
*/
#pragma once
#ifndef WEBCOMPAT_WINSOCK_H
#define WEBCOMPAT_WINSOCK_H

#include "windows.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;

typedef int SOCKET;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct in_addr IN_ADDR;
typedef struct hostent HOSTENT;
typedef struct servent SERVENT;
typedef struct timeval TIMEVAL;
typedef SOCKET *PSOCKET, *LPSOCKET;
typedef SOCKADDR *PSOCKADDR, *LPSOCKADDR;
typedef SOCKADDR_IN *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef IN_ADDR *PIN_ADDR, *LPIN_ADDR;
typedef HOSTENT *PHOSTENT, *LPHOSTENT;
typedef SERVENT *PSERVENT, *LPSERVENT;
typedef TIMEVAL *PTIMEVAL, *LPTIMEVAL;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef closesocket
#define closesocket close
#endif
#ifndef ioctlsocket
#define ioctlsocket ioctl
#endif

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN  128
#define MAKEWORD_WINSOCK_VERSION 0x0202

typedef struct WSAData {
	WORD  wVersion;
	WORD  wHighVersion;
	char  szDescription[WSADESCRIPTION_LEN + 1];
	char  szSystemStatus[WSASYS_STATUS_LEN + 1];
	unsigned short iMaxSockets;
	unsigned short iMaxUdpDg;
	char *lpVendorInfo;
} WSADATA, *LPWSADATA;

#define WSABASEERR           10000
#define WSAEINTR             EINTR
#define WSAEBADF             EBADF
#define WSAEACCES            EACCES
#define WSAEFAULT            EFAULT
#define WSAEMFILE            EMFILE
#define WSAEWOULDBLOCK       EWOULDBLOCK
#define WSAEINPROGRESS       EINPROGRESS
#define WSAEALREADY          EALREADY
#define WSAENOTSOCK          ENOTSOCK
#define WSAEDESTADDRREQ      EDESTADDRREQ
#define WSAEMSGSIZE          EMSGSIZE
#define WSAEPROTOTYPE        EPROTOTYPE
#define WSAENOPROTOOPT       ENOPROTOOPT
#define WSAEPROTONOSUPPORT   EPROTONOSUPPORT
#define WSAESOCKTNOSUPPORT   ESOCKTNOSUPPORT
#define WSAEOPNOTSUPP        EOPNOTSUPP
#define WSAEPFNOSUPPORT      EPFNOSUPPORT
#define WSAEAFNOSUPPORT      EAFNOSUPPORT
#define WSAEADDRINUSE        EADDRINUSE
#define WSAEADDRNOTAVAIL     EADDRNOTAVAIL
#define WSAENETDOWN          ENETDOWN
#define WSAENETUNREACH       ENETUNREACH
#define WSAENETRESET         ENETRESET
#define WSAECONNABORTED      ECONNABORTED
#define WSAECONNRESET        ECONNRESET
#define WSAENOBUFS           ENOBUFS
#define WSAEISCONN           EISCONN
#define WSAENOTCONN          ENOTCONN
#define WSAESHUTDOWN         ESHUTDOWN
#define WSAETOOMANYREFS      ETOOMANYREFS
#define WSAETIMEDOUT         ETIMEDOUT
#define WSAECONNREFUSED      ECONNREFUSED
#define WSAELOOP             ELOOP
#define WSAENAMETOOLONG      ENAMETOOLONG
#define WSAEHOSTDOWN         EHOSTDOWN
#define WSAEHOSTUNREACH      EHOSTUNREACH
#define WSAENOTEMPTY         ENOTEMPTY
#define WSAEPROCLIM          (WSABASEERR + 67) /* no EPROCLIM in this libc */
#define WSAEUSERS            EUSERS
#define WSAEDQUOT            EDQUOT
#define WSAESTALE            ESTALE
#define WSAEREMOTE           EREMOTE
#define WSAEINVAL            EINVAL
#ifndef WSAEDISCON
#define WSAEDISCON           (WSABASEERR + 101)
#endif
#define WSASYSNOTREADY       (WSABASEERR + 91)
#define WSAVERNOTSUPPORTED   (WSABASEERR + 92)
#define WSANOTINITIALISED    (WSABASEERR + 93)
/* The Winsock values (11001..11004), not the resolver's 1..4, which would collide with other codes. */
#define WSAHOST_NOT_FOUND    (WSABASEERR + 1001)
#define WSATRY_AGAIN         (WSABASEERR + 1002)
#define WSANO_RECOVERY       (WSABASEERR + 1003)
#define WSANO_DATA           (WSABASEERR + 1004)

#ifdef __cplusplus
extern "C" {
#endif

int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WSACleanup(void);
int WSAGetLastError(void);
void WSASetLastError(int iError);

#ifdef __cplusplus
}

/* Winsock passes address lengths as int, POSIX as socklen_t. */
extern "C++" {
inline SOCKET accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
	return accept(s, addr, reinterpret_cast<socklen_t *>(addrlen));
}
inline int getsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
	return getsockname(s, name, reinterpret_cast<socklen_t *>(namelen));
}
inline int getpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
	return getpeername(s, name, reinterpret_cast<socklen_t *>(namelen));
}
inline int getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
	return getsockopt(s, level, optname, optval, reinterpret_cast<socklen_t *>(optlen));
}
inline int recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
	return static_cast<int>(recvfrom(s, buf, static_cast<size_t>(len), flags, from, reinterpret_cast<socklen_t *>(fromlen)));
}

/* The length argument is often a literal null, which would match both the
** int and the socklen_t overload. */
inline SOCKET accept(SOCKET s, struct sockaddr *addr, decltype(nullptr))
{
	return accept(s, addr, static_cast<socklen_t *>(nullptr));
}
inline int getsockname(SOCKET s, struct sockaddr *name, decltype(nullptr))
{
	return getsockname(s, name, static_cast<socklen_t *>(nullptr));
}
inline int getpeername(SOCKET s, struct sockaddr *name, decltype(nullptr))
{
	return getpeername(s, name, static_cast<socklen_t *>(nullptr));
}
inline int recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, decltype(nullptr))
{
	return static_cast<int>(recvfrom(s, buf, static_cast<size_t>(len), flags, from, static_cast<socklen_t *>(nullptr)));
}
}
#endif

#endif /* WEBCOMPAT_WINSOCK_H */
