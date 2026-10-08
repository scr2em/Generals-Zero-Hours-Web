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
** WebAssembly port: the Winsock additions to the BSD socket API. The socket
** functions themselves come from Emscripten.
*/
#include "webcompat_internal.h"

#include <winsock.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

extern "C" {

int WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
{
	if (!lpWSAData)
		return WSAEFAULT;
	memset(lpWSAData, 0, sizeof(*lpWSAData));
	lpWSAData->wVersion = wVersionRequested;
	lpWSAData->wHighVersion = 0x0202;
	snprintf(lpWSAData->szDescription, sizeof(lpWSAData->szDescription), "WebAssembly sockets");
	snprintf(lpWSAData->szSystemStatus, sizeof(lpWSAData->szSystemStatus), "Running");
	lpWSAData->iMaxSockets = 1024;
	lpWSAData->iMaxUdpDg = 65507;
	return 0;
}

int WSACleanup(void)
{
	return 0;
}

int WSAGetLastError(void)
{
	// The WSAE* codes are the errno values.
	return errno;
}

void WSASetLastError(int iError)
{
	errno = iError;
}

} // extern "C"
