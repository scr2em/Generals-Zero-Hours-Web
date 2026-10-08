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
** WebAssembly port: the SNMP types of the Windows SDK. The game reads the list
** of TCP connections through the SNMP extension DLLs, which never load in the
** browser; only the declarations are needed.
*/
#pragma once

#include "windows.h"

#ifdef __cplusplus

typedef LONG  AsnInteger32;
typedef AsnInteger32 AsnInteger;
typedef ULONG AsnUnsigned32;
typedef ULARGE_INTEGER Counter64;
typedef AsnUnsigned32 AsnCounter32;
typedef AsnUnsigned32 AsnGauge32;
typedef AsnUnsigned32 AsnTimeticks;

typedef struct {
	BYTE *stream;
	UINT  length;
	BOOL  dynamic;
} AsnOctetString;

typedef struct {
	UINT  idLength;
	UINT *ids;
} AsnObjectIdentifier;

typedef AsnOctetString AsnBits;
typedef AsnOctetString AsnSequence;
typedef AsnOctetString AsnImplicitSequence;
typedef AsnOctetString AsnIPAddress;
typedef AsnOctetString AsnNetworkAddress;
typedef AsnOctetString AsnDisplayString;
typedef AsnOctetString AsnOpaque;
typedef AsnObjectIdentifier AsnObjectName;

typedef struct {
	BYTE asnType;
	union {
		AsnInteger32        number;
		AsnUnsigned32       unsigned32;
		Counter64           counter64;
		AsnOctetString      string;
		AsnBits             bits;
		AsnObjectIdentifier object;
		AsnSequence         sequence;
		AsnIPAddress        address;
		AsnCounter32        counter;
		AsnGauge32          gauge;
		AsnTimeticks        ticks;
		AsnOpaque           arbitrary;
	} asnValue;
} AsnAny;
typedef AsnAny AsnObjectSyntax;

typedef struct {
	AsnObjectName   name;
	AsnObjectSyntax value;
} SnmpVarBind;
typedef SnmpVarBind RFC1157VarBind;

typedef struct {
	SnmpVarBind *list;
	UINT         len;
} SnmpVarBindList;
typedef SnmpVarBindList RFC1157VarBindList;

#define SNMP_PDU_GET     0xA0
#define SNMP_PDU_GETNEXT 0xA1
#define SNMP_PDU_RESPONSE 0xA2
#define SNMP_PDU_SET     0xA3

#endif
