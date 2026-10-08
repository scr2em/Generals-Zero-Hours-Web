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
** WebAssembly port: declarations shared by the WebCompat sources. Not part of
** the public interface.
*/
#pragma once

#include <windows.h>

#include <atomic>
#include <pthread.h>
#include <stdint.h>

namespace WebCompat
{

enum HandleType : uint32_t
{
	HANDLE_FILE = 1,
	HANDLE_EVENT,
	HANDLE_MUTEX,
	HANDLE_SEMAPHORE,
	HANDLE_THREAD,
	HANDLE_FIND,
	HANDLE_MAPPING,
};

// The header of every kernel object that a HANDLE refers to.
struct HandleObject
{
	static const uint32_t MAGIC = 0x484F4357; // "WCOH"

	explicit HandleObject(HandleType t) : magic(MAGIC), type(t), refs(1) {}
	virtual ~HandleObject() { magic = 0; }

	// Waitable objects (events, mutexes, semaphores, threads) override these.
	// Both are called with the global sync lock held. Acquire() takes the
	// object after IsSignaled() returned true (it resets auto-reset events,
	// takes mutex ownership and so on).
	virtual bool IsWaitable() const { return false; }
	virtual bool IsSignaled() { return false; }
	virtual void Acquire() {}

	uint32_t magic;
	HandleType type;
	std::atomic<int> refs;
};

inline HANDLE ToHandle(HandleObject *object) { return reinterpret_cast<HANDLE>(object); }

// Returns the object behind a handle, or null if the handle is not a valid
// handle of the given type (type 0 accepts any).
HandleObject *HandleToObject(HANDLE handle, uint32_t type = 0);
void AddRefObject(HandleObject *object);
void ReleaseObject(HandleObject *object);

// All waitable objects share one lock and one condition variable. State
// changes broadcast the condition.
extern pthread_mutex_t g_syncLock;
extern pthread_cond_t g_syncCond;
void NotifySyncChange();

// Sets the Win32 last error from errno.
void SetLastErrorFromErrno();
DWORD ErrnoToWin32Error(int err);

// Converts a Windows-style path (backslashes, optional drive letter,
// arbitrary case) to the matching path in the POSIX file system. Every
// directory component and the final one are matched case-insensitively
// against what exists. A final component that does not exist is kept as it
// was written, so that new files can be created. Returns true if the whole
// path exists.
bool ResolvePath(const char *path, char *resolved, size_t resolvedSize);

// Converts a time_t / timespec to a FILETIME and back.
void UnixTimeToFileTime(int64_t seconds, int32_t nanoseconds, FILETIME *out);
int64_t FileTimeToUnixSeconds(const FILETIME *in, int32_t *nanoseconds);

} // namespace WebCompat
