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
** WebAssembly port: Win32 synchronisation objects, threads and thread-local
** storage on top of pthreads.
*/
#include "webcompat_internal.h"

#include <errno.h>
#include <map>
#include <sched.h>
#include <string>
#include <time.h>

using namespace WebCompat;

namespace WebCompat
{

pthread_mutex_t g_syncLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_syncCond = PTHREAD_COND_INITIALIZER;

void NotifySyncChange()
{
	pthread_cond_broadcast(&g_syncCond);
}

HandleObject *HandleToObject(HANDLE handle, uint32_t type)
{
	if (!handle || handle == INVALID_HANDLE_VALUE || (reinterpret_cast<uintptr_t>(handle) & 3) != 0)
		return nullptr;
	// Pseudo handles are small negative numbers.
	if (reinterpret_cast<uintptr_t>(handle) > 0xFFFFFFF0u)
		return nullptr;
	HandleObject *object = reinterpret_cast<HandleObject *>(handle);
	if (object->magic != HandleObject::MAGIC)
		return nullptr;
	if (type != 0 && object->type != type)
		return nullptr;
	return object;
}

void AddRefObject(HandleObject *object)
{
	object->refs.fetch_add(1, std::memory_order_relaxed);
}

void ReleaseObject(HandleObject *object)
{
	if (object->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
		delete object;
}

} // namespace WebCompat

namespace
{

// Named objects: events, mutexes and semaphores that can be opened by name.
struct NamedObject : HandleObject
{
	explicit NamedObject(HandleType t) : HandleObject(t) {}
	~NamedObject() override;

	bool IsWaitable() const override { return true; }

	std::string name;
};

pthread_mutex_t s_nameLock = PTHREAD_MUTEX_INITIALIZER;
std::map<std::string, NamedObject *> &NameTable()
{
	static std::map<std::string, NamedObject *> table;
	return table;
}

NamedObject::~NamedObject()
{
	if (!name.empty())
	{
		pthread_mutex_lock(&s_nameLock);
		std::map<std::string, NamedObject *>::iterator it = NameTable().find(name);
		if (it != NameTable().end() && it->second == this)
			NameTable().erase(it);
		pthread_mutex_unlock(&s_nameLock);
	}
}

// Looks the name up, and returns a new reference to the object if it exists.
NamedObject *FindNamed(const char *name, HandleType type)
{
	NamedObject *found = nullptr;
	pthread_mutex_lock(&s_nameLock);
	std::map<std::string, NamedObject *>::iterator it = NameTable().find(name);
	if (it != NameTable().end() && it->second->type == type)
	{
		found = it->second;
		// The object may be on its way out: only take it if it is still alive.
		int refs = found->refs.load();
		while (refs > 0 && !found->refs.compare_exchange_weak(refs, refs + 1))
		{
		}
		if (refs <= 0)
			found = nullptr;
	}
	pthread_mutex_unlock(&s_nameLock);
	return found;
}

// Registers a freshly created object under a name. If another object has the
// name, returns that one instead (with a reference) and deletes the new one.
NamedObject *RegisterNamed(NamedObject *object, const char *name, bool *alreadyExisted)
{
	*alreadyExisted = false;
	if (!name || !*name)
		return object;
	if (NamedObject *existing = FindNamed(name, object->type))
	{
		*alreadyExisted = true;
		delete object;
		return existing;
	}
	pthread_mutex_lock(&s_nameLock);
	object->name = name;
	NameTable()[object->name] = object;
	pthread_mutex_unlock(&s_nameLock);
	return object;
}

/* -------------------------------------------------------------------------
** Event, mutex, semaphore
** ----------------------------------------------------------------------- */

struct EventObject : NamedObject
{
	EventObject(bool manual, bool initial) : NamedObject(HANDLE_EVENT), manualReset(manual), signaled(initial) {}

	bool IsSignaled() override { return signaled; }
	void Acquire() override
	{
		if (!manualReset)
			signaled = false;
	}

	bool manualReset;
	bool signaled;
};

uint32_t CurrentThreadId();

struct MutexObject : NamedObject
{
	MutexObject() : NamedObject(HANDLE_MUTEX), owner(0), recursion(0) {}

	bool IsSignaled() override { return owner == 0 || owner == CurrentThreadId(); }
	void Acquire() override
	{
		owner = CurrentThreadId();
		++recursion;
	}

	uint32_t owner;
	int recursion;
};

struct SemaphoreObject : NamedObject
{
	SemaphoreObject(LONG initial, LONG maximum) : NamedObject(HANDLE_SEMAPHORE), count(initial), maxCount(maximum) {}

	bool IsSignaled() override { return count > 0; }
	void Acquire() override { --count; }

	LONG count;
	LONG maxCount;
};

/* -------------------------------------------------------------------------
** Threads
** ----------------------------------------------------------------------- */

std::atomic<uint32_t> s_nextThreadId(1);
thread_local uint32_t t_threadId = 0;

uint32_t CurrentThreadId()
{
	if (!t_threadId)
		t_threadId = s_nextThreadId.fetch_add(1);
	return t_threadId;
}

struct ThreadObject : HandleObject
{
	ThreadObject() : HandleObject(HANDLE_THREAD), id(0), exitCode(STILL_ACTIVE), finished(false), started(true), start(nullptr), parameter(nullptr), priority(0), thread() {}

	bool IsWaitable() const override { return true; }
	bool IsSignaled() override { return finished; }

	uint32_t id;
	DWORD exitCode;
	bool finished;
	bool started;
	LPTHREAD_START_ROUTINE start;
	LPVOID parameter;
	int priority;
	pthread_t thread;
};

thread_local ThreadObject *t_currentThread = nullptr;

void FinishThread(ThreadObject *self, DWORD exitCode)
{
	pthread_mutex_lock(&g_syncLock);
	self->exitCode = exitCode;
	self->finished = true;
	NotifySyncChange();
	pthread_mutex_unlock(&g_syncLock);
}

void *ThreadMain(void *arg)
{
	ThreadObject *self = static_cast<ThreadObject *>(arg);
	t_threadId = self->id;
	t_currentThread = self;

	pthread_mutex_lock(&g_syncLock);
	while (!self->started)
		pthread_cond_wait(&g_syncCond, &g_syncLock);
	pthread_mutex_unlock(&g_syncLock);

	const DWORD result = self->start(self->parameter);
	FinishThread(self, result);
	t_currentThread = nullptr;
	ReleaseObject(self); // the reference that the running thread owns
	return nullptr;
}

/* -------------------------------------------------------------------------
** Waiting
** ----------------------------------------------------------------------- */

// Absolute CLOCK_REALTIME deadline for a wait of the given length.
void MakeDeadline(DWORD milliseconds, struct timespec *deadline)
{
	clock_gettime(CLOCK_REALTIME, deadline);
	deadline->tv_sec += milliseconds / 1000;
	deadline->tv_nsec += (long)(milliseconds % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_nsec -= 1000000000L;
		++deadline->tv_sec;
	}
}

// Waits on the sync condition. Returns false if the deadline passed. The sync
// lock must be held.
bool WaitForChange(DWORD milliseconds, const struct timespec *deadline)
{
	if (milliseconds == INFINITE)
	{
		pthread_cond_wait(&g_syncCond, &g_syncLock);
		return true;
	}
	return pthread_cond_timedwait(&g_syncCond, &g_syncLock, deadline) != ETIMEDOUT;
}

DWORD WaitForObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD milliseconds)
{
	if (count == 0 || count > 64)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return WAIT_FAILED;
	}
	HandleObject *objects[64];
	for (DWORD i = 0; i < count; ++i)
	{
		HANDLE handle = handles[i];
		if (handle == (HANDLE)(LONG_PTR)-2 && t_currentThread)
		{
			objects[i] = t_currentThread;
			continue;
		}
		objects[i] = HandleToObject(handle);
		if (!objects[i] || !objects[i]->IsWaitable())
		{
			SetLastError(ERROR_INVALID_HANDLE);
			return WAIT_FAILED;
		}
	}

	struct timespec deadline;
	if (milliseconds != INFINITE)
		MakeDeadline(milliseconds, &deadline);

	pthread_mutex_lock(&g_syncLock);
	for (;;)
	{
		if (waitAll)
		{
			bool all = true;
			for (DWORD i = 0; i < count && all; ++i)
				all = objects[i]->IsSignaled();
			if (all)
			{
				for (DWORD i = 0; i < count; ++i)
					objects[i]->Acquire();
				NotifySyncChange();
				pthread_mutex_unlock(&g_syncLock);
				return WAIT_OBJECT_0;
			}
		}
		else
		{
			for (DWORD i = 0; i < count; ++i)
			{
				if (objects[i]->IsSignaled())
				{
					objects[i]->Acquire();
					NotifySyncChange();
					pthread_mutex_unlock(&g_syncLock);
					return WAIT_OBJECT_0 + i;
				}
			}
		}
		if (milliseconds == 0)
			break;
		if (!WaitForChange(milliseconds, &deadline))
			break;
	}
	pthread_mutex_unlock(&g_syncLock);
	return WAIT_TIMEOUT;
}

// Thread-local storage: slots are pthread keys.
const DWORD MAX_TLS_SLOTS = 1088;
pthread_key_t s_tlsKeys[MAX_TLS_SLOTS];
bool s_tlsUsed[MAX_TLS_SLOTS];
pthread_mutex_t s_tlsLock = PTHREAD_MUTEX_INITIALIZER;

} // namespace

/* ---------------------------------------------------------------------------
** Critical sections
** ------------------------------------------------------------------------- */

extern "C" {

void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_t *mutex = static_cast<pthread_mutex_t *>(malloc(sizeof(pthread_mutex_t)));
	pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	lpCriticalSection->Impl = mutex;
	lpCriticalSection->LockCount = -1;
	lpCriticalSection->RecursionCount = 0;
	lpCriticalSection->OwningThread = nullptr;
	lpCriticalSection->LockSemaphore = nullptr;
	lpCriticalSection->SpinCount = 0;
}

BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount)
{
	InitializeCriticalSection(lpCriticalSection);
	lpCriticalSection->SpinCount = dwSpinCount;
	return TRUE;
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	pthread_mutex_t *mutex = static_cast<pthread_mutex_t *>(lpCriticalSection->Impl);
	if (mutex)
	{
		pthread_mutex_destroy(mutex);
		free(mutex);
		lpCriticalSection->Impl = nullptr;
	}
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	pthread_mutex_lock(static_cast<pthread_mutex_t *>(lpCriticalSection->Impl));
	++lpCriticalSection->RecursionCount;
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	if (pthread_mutex_trylock(static_cast<pthread_mutex_t *>(lpCriticalSection->Impl)) != 0)
		return FALSE;
	++lpCriticalSection->RecursionCount;
	return TRUE;
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	--lpCriticalSection->RecursionCount;
	pthread_mutex_unlock(static_cast<pthread_mutex_t *>(lpCriticalSection->Impl));
}

/* ---------------------------------------------------------------------------
** Interlocked operations
** ------------------------------------------------------------------------- */

LONG WINAPI InterlockedIncrement(LONG volatile *lpAddend)
{
	return __atomic_add_fetch(lpAddend, 1, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedDecrement(LONG volatile *lpAddend)
{
	return __atomic_sub_fetch(lpAddend, 1, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedExchange(LONG volatile *Target, LONG Value)
{
	return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedExchangeAdd(LONG volatile *Addend, LONG Value)
{
	return __atomic_fetch_add(Addend, Value, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedCompareExchange(LONG volatile *Destination, LONG Exchange, LONG Comperand)
{
	__atomic_compare_exchange_n(Destination, &Comperand, Exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return Comperand;
}

/* ---------------------------------------------------------------------------
** Mutexes, events, semaphores
** ------------------------------------------------------------------------- */

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL bInitialOwner, LPCSTR lpName)
{
	MutexObject *created = new MutexObject();
	bool existed;
	NamedObject *object = RegisterNamed(created, lpName, &existed);
	SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	if (bInitialOwner && !existed)
	{
		pthread_mutex_lock(&g_syncLock);
		object->Acquire();
		pthread_mutex_unlock(&g_syncLock);
	}
	return ToHandle(object);
}

HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR lpName)
{
	NamedObject *object = lpName ? FindNamed(lpName, HANDLE_MUTEX) : nullptr;
	if (!object)
		SetLastError(ERROR_FILE_NOT_FOUND);
	return ToHandle(object);
}

BOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
	MutexObject *mutex = static_cast<MutexObject *>(HandleToObject(hMutex, HANDLE_MUTEX));
	if (!mutex)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	BOOL result = TRUE;
	pthread_mutex_lock(&g_syncLock);
	if (mutex->owner != CurrentThreadId() || mutex->recursion == 0)
	{
		result = FALSE;
	}
	else if (--mutex->recursion == 0)
	{
		mutex->owner = 0;
		NotifySyncChange();
	}
	pthread_mutex_unlock(&g_syncLock);
	if (!result)
		SetLastError(288); // ERROR_NOT_OWNER
	return result;
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
	EventObject *created = new EventObject(bManualReset != FALSE, bInitialState != FALSE);
	bool existed;
	NamedObject *object = RegisterNamed(created, lpName, &existed);
	SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return ToHandle(object);
}

HANDLE WINAPI OpenEventA(DWORD, BOOL, LPCSTR lpName)
{
	NamedObject *object = lpName ? FindNamed(lpName, HANDLE_EVENT) : nullptr;
	if (!object)
		SetLastError(ERROR_FILE_NOT_FOUND);
	return ToHandle(object);
}

static BOOL SetEventState(HANDLE hEvent, bool signaled, bool pulse)
{
	EventObject *event = static_cast<EventObject *>(HandleToObject(hEvent, HANDLE_EVENT));
	if (!event)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	pthread_mutex_lock(&g_syncLock);
	event->signaled = signaled;
	NotifySyncChange();
	if (pulse)
	{
		// Let the waiters that are already blocked see the signal.
		pthread_mutex_unlock(&g_syncLock);
		sched_yield();
		pthread_mutex_lock(&g_syncLock);
		event->signaled = false;
	}
	pthread_mutex_unlock(&g_syncLock);
	return TRUE;
}

BOOL WINAPI SetEvent(HANDLE hEvent)
{
	return SetEventState(hEvent, true, false);
}

BOOL WINAPI ResetEvent(HANDLE hEvent)
{
	return SetEventState(hEvent, false, false);
}

BOOL WINAPI PulseEvent(HANDLE hEvent)
{
	return SetEventState(hEvent, true, true);
}

HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG lInitialCount, LONG lMaximumCount, LPCSTR lpName)
{
	if (lMaximumCount <= 0 || lInitialCount < 0 || lInitialCount > lMaximumCount)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	SemaphoreObject *created = new SemaphoreObject(lInitialCount, lMaximumCount);
	bool existed;
	NamedObject *object = RegisterNamed(created, lpName, &existed);
	SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return ToHandle(object);
}

BOOL WINAPI ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount)
{
	SemaphoreObject *semaphore = static_cast<SemaphoreObject *>(HandleToObject(hSemaphore, HANDLE_SEMAPHORE));
	if (!semaphore || lReleaseCount <= 0)
	{
		SetLastError(semaphore ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
		return FALSE;
	}
	BOOL result = TRUE;
	pthread_mutex_lock(&g_syncLock);
	if (semaphore->count + lReleaseCount > semaphore->maxCount)
	{
		result = FALSE;
	}
	else
	{
		if (lpPreviousCount)
			*lpPreviousCount = semaphore->count;
		semaphore->count += lReleaseCount;
		NotifySyncChange();
	}
	pthread_mutex_unlock(&g_syncLock);
	if (!result)
		SetLastError(298); // ERROR_TOO_MANY_POSTS
	return result;
}

DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
	return WaitForObjects(1, &hHandle, FALSE, dwMilliseconds);
}

DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
	return WaitForObjects(nCount, lpHandles, bWaitAll, dwMilliseconds);
}

BOOL WINAPI CloseHandle(HANDLE hObject)
{
	HandleObject *object = HandleToObject(hObject);
	if (!object)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	ReleaseObject(object);
	return TRUE;
}

BOOL WINAPI DuplicateHandle(HANDLE, HANDLE hSourceHandle, HANDLE, LPHANDLE lpTargetHandle, DWORD, BOOL, DWORD)
{
	HandleObject *object = HandleToObject(hSourceHandle);
	if (!object || !lpTargetHandle)
	{
		// Pseudo handles duplicate to themselves.
		if (lpTargetHandle && (hSourceHandle == (HANDLE)(LONG_PTR)-1 || hSourceHandle == (HANDLE)(LONG_PTR)-2))
		{
			*lpTargetHandle = hSourceHandle;
			return TRUE;
		}
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	AddRefObject(object);
	*lpTargetHandle = hSourceHandle;
	return TRUE;
}

/* ---------------------------------------------------------------------------
** Threads and processes
** ------------------------------------------------------------------------- */

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
	if (!lpStartAddress)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	ThreadObject *thread = new ThreadObject();
	thread->id = s_nextThreadId.fetch_add(1);
	thread->start = lpStartAddress;
	thread->parameter = lpParameter;
	thread->started = (dwCreationFlags & CREATE_SUSPENDED) == 0;
	AddRefObject(thread); // owned by the running thread

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (dwStackSize >= 65536)
		pthread_attr_setstacksize(&attr, dwStackSize);
	const int error = pthread_create(&thread->thread, &attr, ThreadMain, thread);
	pthread_attr_destroy(&attr);
	if (error != 0)
	{
		ReleaseObject(thread);
		ReleaseObject(thread);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return nullptr;
	}
	if (lpThreadId)
		*lpThreadId = thread->id;
	return ToHandle(thread);
}

HANDLE WINAPI GetCurrentThread(void)
{
	return (HANDLE)(LONG_PTR)-2;
}

DWORD WINAPI GetCurrentThreadId(void)
{
	return CurrentThreadId();
}

HANDLE WINAPI GetCurrentProcess(void)
{
	return (HANDLE)(LONG_PTR)-1;
}

DWORD WINAPI GetCurrentProcessId(void)
{
	return 1;
}

static ThreadObject *ThreadFromHandle(HANDLE hThread)
{
	if (hThread == (HANDLE)(LONG_PTR)-2)
		return t_currentThread;
	return static_cast<ThreadObject *>(HandleToObject(hThread, HANDLE_THREAD));
}

BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority)
{
	// The browser does not expose thread priorities, only remember the value.
	ThreadObject *thread = ThreadFromHandle(hThread);
	if (thread)
		thread->priority = nPriority;
	return TRUE;
}

int WINAPI GetThreadPriority(HANDLE hThread)
{
	ThreadObject *thread = ThreadFromHandle(hThread);
	return thread ? thread->priority : THREAD_PRIORITY_NORMAL;
}

BOOL WINAPI SetPriorityClass(HANDLE, DWORD)
{
	return TRUE;
}

DWORD WINAPI GetPriorityClass(HANDLE)
{
	return NORMAL_PRIORITY_CLASS;
}

BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode)
{
	ThreadObject *thread = static_cast<ThreadObject *>(HandleToObject(hThread, HANDLE_THREAD));
	if (!thread)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (!thread->finished)
	{
		pthread_cancel(thread->thread);
		FinishThread(thread, dwExitCode);
	}
	return TRUE;
}

BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)
{
	ThreadObject *thread = ThreadFromHandle(hThread);
	if (!thread || !lpExitCode)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	pthread_mutex_lock(&g_syncLock);
	*lpExitCode = thread->exitCode;
	pthread_mutex_unlock(&g_syncLock);
	return TRUE;
}

void WINAPI ExitThread(DWORD dwExitCode)
{
	ThreadObject *self = t_currentThread;
	if (self)
	{
		FinishThread(self, dwExitCode);
		t_currentThread = nullptr;
		ReleaseObject(self);
	}
	pthread_exit(nullptr);
}

DWORD WINAPI SuspendThread(HANDLE)
{
	// Threads cannot be suspended from the outside in the browser.
	return (DWORD)-1;
}

DWORD WINAPI ResumeThread(HANDLE hThread)
{
	ThreadObject *thread = static_cast<ThreadObject *>(HandleToObject(hThread, HANDLE_THREAD));
	if (!thread)
		return (DWORD)-1;
	pthread_mutex_lock(&g_syncLock);
	const DWORD previousCount = thread->started ? 0 : 1;
	thread->started = true;
	NotifySyncChange();
	pthread_mutex_unlock(&g_syncLock);
	return previousCount;
}

BOOL WINAPI GetThreadContext(HANDLE, LPCONTEXT)
{
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}

BOOL WINAPI SwitchToThread(void)
{
	sched_yield();
	return TRUE;
}

void WINAPI ExitProcess(UINT uExitCode)
{
	exit((int)uExitCode);
}

BOOL WINAPI TerminateProcess(HANDLE, UINT uExitCode)
{
	_exit((int)uExitCode);
}

BOOL WINAPI GetExitCodeProcess(HANDLE, LPDWORD lpExitCode)
{
	if (lpExitCode)
		*lpExitCode = STILL_ACTIVE;
	return TRUE;
}

BOOL WINAPI CreateProcessA(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFO, LPPROCESS_INFORMATION)
{
	// A browser tab cannot start other programs.
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}

BOOL WINAPI CreateProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION)
{
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}

BOOL WINAPI CreatePipe(PHANDLE hReadPipe, PHANDLE hWritePipe, LPSECURITY_ATTRIBUTES, DWORD)
{
	// Pipes only connect programs; there is no second program to connect.
	if (hReadPipe)
		*hReadPipe = nullptr;
	if (hWritePipe)
		*hWritePipe = nullptr;
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}

BOOL WINAPI PeekNamedPipe(HANDLE, LPVOID, DWORD, LPDWORD, LPDWORD, LPDWORD)
{
	SetLastError(ERROR_INVALID_HANDLE);
	return FALSE;
}

BOOL WINAPI SetHandleInformation(HANDLE, DWORD, DWORD)
{
	// Handles are never inherited, there are no child processes.
	return TRUE;
}

HANDLE WINAPI CreateJobObjectW(LPSECURITY_ATTRIBUTES, LPCWSTR)
{
	SetLastError(ERROR_ACCESS_DENIED);
	return nullptr;
}

BOOL WINAPI SetInformationJobObject(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD)
{
	SetLastError(ERROR_INVALID_HANDLE);
	return FALSE;
}

BOOL WINAPI AssignProcessToJobObject(HANDLE, HANDLE)
{
	SetLastError(ERROR_INVALID_HANDLE);
	return FALSE;
}

LPSTR WINAPI GetCommandLineA(void)
{
	// The program name and arguments, quoted as Windows does.
	static std::string s_commandLine;
	s_commandLine.clear();
	if (__argc <= 0 || !__argv)
		return const_cast<char *>("generalszh.exe");
	for (int i = 0; i < __argc; ++i)
	{
		if (i)
			s_commandLine += ' ';
		const bool quote = strchr(__argv[i], ' ') != nullptr;
		s_commandLine += quote ? "\"" : "";
		s_commandLine += __argv[i];
		s_commandLine += quote ? "\"" : "";
	}
	return const_cast<char *>(s_commandLine.c_str());
}

DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
	const char *value = lpName ? getenv(lpName) : nullptr;
	if (!value)
	{
		SetLastError(203); // ERROR_ENVVAR_NOT_FOUND
		return 0;
	}
	const size_t length = strlen(value);
	if (length + 1 > nSize)
		return (DWORD)(length + 1);
	memcpy(lpBuffer, value, length + 1);
	return (DWORD)length;
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
	if (!lpName)
		return FALSE;
	if (!lpValue)
		return unsetenv(lpName) == 0;
	return setenv(lpName, lpValue, 1) == 0;
}

/* ---------------------------------------------------------------------------
** Thread-local storage
** ------------------------------------------------------------------------- */

DWORD WINAPI TlsAlloc(void)
{
	DWORD result = TLS_OUT_OF_INDEXES;
	pthread_mutex_lock(&s_tlsLock);
	for (DWORD i = 0; i < MAX_TLS_SLOTS; ++i)
	{
		if (!s_tlsUsed[i])
		{
			if (pthread_key_create(&s_tlsKeys[i], nullptr) == 0)
			{
				s_tlsUsed[i] = true;
				result = i;
			}
			break;
		}
	}
	pthread_mutex_unlock(&s_tlsLock);
	if (result == TLS_OUT_OF_INDEXES)
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return result;
}

BOOL WINAPI TlsFree(DWORD dwTlsIndex)
{
	BOOL result = FALSE;
	pthread_mutex_lock(&s_tlsLock);
	if (dwTlsIndex < MAX_TLS_SLOTS && s_tlsUsed[dwTlsIndex])
	{
		pthread_key_delete(s_tlsKeys[dwTlsIndex]);
		s_tlsUsed[dwTlsIndex] = false;
		result = TRUE;
	}
	pthread_mutex_unlock(&s_tlsLock);
	return result;
}

LPVOID WINAPI TlsGetValue(DWORD dwTlsIndex)
{
	if (dwTlsIndex >= MAX_TLS_SLOTS || !s_tlsUsed[dwTlsIndex])
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return nullptr;
	}
	SetLastError(ERROR_SUCCESS);
	return pthread_getspecific(s_tlsKeys[dwTlsIndex]);
}

BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)
{
	if (dwTlsIndex >= MAX_TLS_SLOTS || !s_tlsUsed[dwTlsIndex])
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	return pthread_setspecific(s_tlsKeys[dwTlsIndex], lpTlsValue) == 0;
}

} // extern "C"

/* ---------------------------------------------------------------------------
** The C runtime's thread functions (process.h)
** ------------------------------------------------------------------------- */

namespace
{

struct CrtThreadStart
{
	unsigned (__stdcall *function)(void *);
	void (__cdecl *simpleFunction)(void *);
	void *argument;
};

DWORD WINAPI CrtThreadEntry(LPVOID parameter)
{
	CrtThreadStart start = *static_cast<CrtThreadStart *>(parameter);
	delete static_cast<CrtThreadStart *>(parameter);
	if (start.function)
		return start.function(start.argument);
	start.simpleFunction(start.argument);
	return 0;
}

} // namespace

extern "C" {

uintptr_t _beginthreadex(void *security, unsigned stack_size, unsigned (__stdcall *start_address)(void *), void *arglist, unsigned initflag, unsigned *thrdaddr)
{
	CrtThreadStart *start = new CrtThreadStart();
	start->function = start_address;
	start->simpleFunction = nullptr;
	start->argument = arglist;
	DWORD id = 0;
	HANDLE handle = CreateThread(static_cast<LPSECURITY_ATTRIBUTES>(security), stack_size, CrtThreadEntry, start, initflag, &id);
	if (!handle)
	{
		delete start;
		return 0;
	}
	if (thrdaddr)
		*thrdaddr = id;
	return reinterpret_cast<uintptr_t>(handle);
}

uintptr_t _beginthread(void (__cdecl *start_address)(void *), unsigned stack_size, void *arglist)
{
	CrtThreadStart *start = new CrtThreadStart();
	start->function = nullptr;
	start->simpleFunction = start_address;
	start->argument = arglist;
	HANDLE handle = CreateThread(nullptr, stack_size, CrtThreadEntry, start, 0, nullptr);
	if (!handle)
	{
		delete start;
		return (uintptr_t)-1;
	}
	// The handle of a _beginthread thread is closed when the thread ends.
	// Nobody waits on it, so it is not handed out beyond this value.
	return reinterpret_cast<uintptr_t>(handle);
}

void _endthread(void)
{
	ExitThread(0);
}

void _endthreadex(unsigned retval)
{
	ExitThread(retval);
}

} // extern "C"
