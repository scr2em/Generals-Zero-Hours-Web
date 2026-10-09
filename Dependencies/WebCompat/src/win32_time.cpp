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
** WebAssembly port: Win32 time, timer and time zone functions, and the
** multimedia timer API.
*/
#include "webcompat_internal.h"

#include <mmsystem.h>

#include <sched.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace
{

// 100ns intervals between 1601-01-01 and 1970-01-01.
const int64_t EPOCH_DIFFERENCE_100NS = 116444736000000000LL;

int64_t MonotonicNanoseconds()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// The tick count starts near zero when the program starts.
const int64_t s_startNanoseconds = MonotonicNanoseconds();

const char *const s_dayNames[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *const s_monthNames[] = { "January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December" };

void FillSystemTime(const struct tm &tm, int milliseconds, SYSTEMTIME *st)
{
	st->wYear = (WORD)(tm.tm_year + 1900);
	st->wMonth = (WORD)(tm.tm_mon + 1);
	st->wDayOfWeek = (WORD)tm.tm_wday;
	st->wDay = (WORD)tm.tm_mday;
	st->wHour = (WORD)tm.tm_hour;
	st->wMinute = (WORD)tm.tm_min;
	st->wSecond = (WORD)tm.tm_sec;
	st->wMilliseconds = (WORD)milliseconds;
}

bool IsLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// Days from 1970-01-01 to the given civil date (proleptic Gregorian).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

// The reverse of DaysFromCivil.
void CivilFromDays(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = (unsigned)(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	*y = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y += *m <= 2;
}

// Appends one character, honouring the size of the destination. Returns the
// number of characters the full output needs.
struct OutputBuffer
{
	char *buffer;
	int capacity;
	int length;

	void Put(char c)
	{
		if (buffer && length < capacity)
			buffer[length] = c;
		++length;
	}
	void Put(const char *s)
	{
		while (*s)
			Put(*s++);
	}
	void PutAbbreviation(const char *name)
	{
		for (int i = 0; i < 3 && name[i]; ++i)
			Put(name[i]);
	}
	void PutNumber(int value, int minDigits)
	{
		char digits[16];
		snprintf(digits, sizeof(digits), "%0*d", minDigits, value);
		Put(digits);
	}
};

// Formats a date or time with a Win32 format picture ("dddd, MMMM d, yyyy" or
// "h:mm:ss tt"). Characters in single quotes are literal.
int FormatPicture(const SYSTEMTIME *st, const char *format, char *out, int capacity)
{
	OutputBuffer o = { out, capacity, 0 };
	for (const char *p = format; *p;)
	{
		const char c = *p;
		if (c == '\'')
		{
			++p;
			while (*p && *p != '\'')
				o.Put(*p++);
			if (*p)
				++p;
			continue;
		}
		int run = 1;
		while (p[run] == c)
			++run;
		switch (c)
		{
		case 'd':
			if (run == 1) o.PutNumber(st->wDay, 1);
			else if (run == 2) o.PutNumber(st->wDay, 2);
			else if (run == 3) o.PutAbbreviation(s_dayNames[st->wDayOfWeek % 7]);
			else o.Put(s_dayNames[st->wDayOfWeek % 7]);
			break;
		case 'M':
			if (run == 1) o.PutNumber(st->wMonth, 1);
			else if (run == 2) o.PutNumber(st->wMonth, 2);
			else if (run == 3) o.PutAbbreviation(s_monthNames[(st->wMonth + 11) % 12]);
			else o.Put(s_monthNames[(st->wMonth + 11) % 12]);
			break;
		case 'y':
			if (run <= 2) o.PutNumber(st->wYear % 100, run);
			else o.PutNumber(st->wYear, 4);
			break;
		case 'h':
			o.PutNumber(st->wHour % 12 == 0 ? 12 : st->wHour % 12, run == 1 ? 1 : 2);
			break;
		case 'H':
			o.PutNumber(st->wHour, run == 1 ? 1 : 2);
			break;
		case 'm':
			o.PutNumber(st->wMinute, run == 1 ? 1 : 2);
			break;
		case 's':
			o.PutNumber(st->wSecond, run == 1 ? 1 : 2);
			break;
		case 't':
			if (run == 1) o.Put(st->wHour < 12 ? "A" : "P");
			else o.Put(st->wHour < 12 ? "AM" : "PM");
			break;
		default:
			for (int i = 0; i < run; ++i)
				o.Put(c);
			break;
		}
		p += run;
	}
	// The terminator.
	if (out && o.length < capacity)
		out[o.length] = 0;
	if (out && capacity > 0 && o.length >= capacity)
		return 0;
	return o.length + 1;
}

int FormatDateTime(const SYSTEMTIME *st, const char *format, const char *defaultFormat, char *out, int capacity)
{
	SYSTEMTIME now;
	if (!st)
	{
		GetLocalTime(&now);
		st = &now;
	}
	// The day of the week follows from the date, whatever the caller put in.
	SYSTEMTIME dated = *st;
	dated.wDayOfWeek = (WORD)((DaysFromCivil(st->wYear, st->wMonth ? st->wMonth : 1, st->wDay ? st->wDay : 1) % 7 + 11) % 7);
	st = &dated;
	if (capacity < 0 || (capacity > 0 && !out))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const int result = FormatPicture(st, format ? format : defaultFormat, capacity ? out : nullptr, capacity);
	if (capacity != 0 && result == 0)
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
	return result;
}

int WidenFormat(const WCHAR *wide, char *narrow, size_t size)
{
	size_t i = 0;
	for (; wide[i] && i + 1 < size; ++i)
		narrow[i] = wide[i] < 0x80 ? (char)wide[i] : '?';
	narrow[i] = 0;
	return (int)i;
}

} // namespace

namespace WebCompat
{

void UnixTimeToFileTime(int64_t seconds, int32_t nanoseconds, FILETIME *out)
{
	const int64_t value = seconds * 10000000LL + nanoseconds / 100 + EPOCH_DIFFERENCE_100NS;
	out->dwLowDateTime = (DWORD)(value & 0xFFFFFFFFu);
	out->dwHighDateTime = (DWORD)((uint64_t)value >> 32);
}

int64_t FileTimeToUnixSeconds(const FILETIME *in, int32_t *nanoseconds)
{
	const int64_t value = (int64_t)(((uint64_t)in->dwHighDateTime << 32) | in->dwLowDateTime) - EPOCH_DIFFERENCE_100NS;
	int64_t seconds = value / 10000000LL;
	int64_t remainder = value % 10000000LL;
	if (remainder < 0)
	{
		remainder += 10000000LL;
		--seconds;
	}
	if (nanoseconds)
		*nanoseconds = (int32_t)(remainder * 100);
	return seconds;
}

} // namespace WebCompat

extern "C" {

DWORD WINAPI GetTickCount(void)
{
	return (DWORD)((MonotonicNanoseconds() - s_startNanoseconds) / 1000000);
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
	if (!lpPerformanceCount)
		return FALSE;
	// Microsecond resolution, so that the counter stays within 32 bits of
	// precision for code that converts it to seconds with float arithmetic.
	lpPerformanceCount->QuadPart = MonotonicNanoseconds() / 1000;
	return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
	if (!lpFrequency)
		return FALSE;
	lpFrequency->QuadPart = 1000000;
	return TRUE;
}

void WINAPI Sleep(DWORD dwMilliseconds)
{
	if (dwMilliseconds == 0)
	{
		sched_yield();
		return;
	}
	struct timespec ts;
	ts.tv_sec = dwMilliseconds / 1000;
	ts.tv_nsec = (long)(dwMilliseconds % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
	{
	}
}

DWORD WINAPI SleepEx(DWORD dwMilliseconds, BOOL)
{
	Sleep(dwMilliseconds);
	return 0;
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	time_t seconds = ts.tv_sec;
	localtime_r(&seconds, &tm);
	FillSystemTime(tm, (int)(ts.tv_nsec / 1000000), lpSystemTime);
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	time_t seconds = ts.tv_sec;
	gmtime_r(&seconds, &tm);
	FillSystemTime(tm, (int)(ts.tv_nsec / 1000000), lpSystemTime);
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	WebCompat::UnixTimeToFileTime(ts.tv_sec, (int32_t)ts.tv_nsec, lpSystemTimeAsFileTime);
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)
{
	if (!lpSystemTime || !lpFileTime || lpSystemTime->wMonth < 1 || lpSystemTime->wMonth > 12 ||
		lpSystemTime->wDay < 1 || lpSystemTime->wDay > 31 || lpSystemTime->wHour > 23 ||
		lpSystemTime->wMinute > 59 || lpSystemTime->wSecond > 59 || lpSystemTime->wMilliseconds > 999)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	const int64_t days = DaysFromCivil(lpSystemTime->wYear, lpSystemTime->wMonth, lpSystemTime->wDay);
	const int64_t seconds = days * 86400 + lpSystemTime->wHour * 3600 + lpSystemTime->wMinute * 60 + lpSystemTime->wSecond;
	WebCompat::UnixTimeToFileTime(seconds, lpSystemTime->wMilliseconds * 1000000, lpFileTime);
	return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)
{
	if (!lpFileTime || !lpSystemTime)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	int32_t nanoseconds;
	const int64_t seconds = WebCompat::FileTimeToUnixSeconds(lpFileTime, &nanoseconds);
	int64_t days = seconds / 86400;
	int64_t secondsOfDay = seconds % 86400;
	if (secondsOfDay < 0)
	{
		secondsOfDay += 86400;
		--days;
	}
	int64_t year;
	unsigned month, day;
	CivilFromDays(days, &year, &month, &day);
	lpSystemTime->wYear = (WORD)year;
	lpSystemTime->wMonth = (WORD)month;
	lpSystemTime->wDay = (WORD)day;
	lpSystemTime->wDayOfWeek = (WORD)(((days % 7) + 11) % 7); // 1970-01-01 was a Thursday
	lpSystemTime->wHour = (WORD)(secondsOfDay / 3600);
	lpSystemTime->wMinute = (WORD)(secondsOfDay % 3600 / 60);
	lpSystemTime->wSecond = (WORD)(secondsOfDay % 60);
	lpSystemTime->wMilliseconds = (WORD)(nanoseconds / 1000000);
	return TRUE;
}

static LONG LocalBiasSeconds(time_t seconds)
{
	struct tm tm;
	localtime_r(&seconds, &tm);
	return (LONG)tm.tm_gmtoff;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
	int32_t nanoseconds;
	const int64_t seconds = WebCompat::FileTimeToUnixSeconds(lpFileTime, &nanoseconds);
	WebCompat::UnixTimeToFileTime(seconds + LocalBiasSeconds((time_t)seconds), nanoseconds, lpLocalFileTime);
	return TRUE;
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime)
{
	int32_t nanoseconds;
	const int64_t seconds = WebCompat::FileTimeToUnixSeconds(lpLocalFileTime, &nanoseconds);
	WebCompat::UnixTimeToFileTime(seconds - LocalBiasSeconds((time_t)seconds), nanoseconds, lpFileTime);
	return TRUE;
}

BOOL WINAPI FileTimeToDosDateTime(const FILETIME *lpFileTime, LPWORD lpFatDate, LPWORD lpFatTime)
{
	SYSTEMTIME st;
	if (!FileTimeToSystemTime(lpFileTime, &st) || st.wYear < 1980 || st.wYear > 2107)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	*lpFatDate = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
	*lpFatTime = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
	return TRUE;
}

BOOL WINAPI DosDateTimeToFileTime(WORD wFatDate, WORD wFatTime, LPFILETIME lpFileTime)
{
	SYSTEMTIME st;
	memset(&st, 0, sizeof(st));
	st.wYear = (WORD)(((wFatDate >> 9) & 0x7F) + 1980);
	st.wMonth = (WORD)((wFatDate >> 5) & 0x0F);
	st.wDay = (WORD)(wFatDate & 0x1F);
	st.wHour = (WORD)((wFatTime >> 11) & 0x1F);
	st.wMinute = (WORD)((wFatTime >> 5) & 0x3F);
	st.wSecond = (WORD)((wFatTime & 0x1F) * 2);
	return SystemTimeToFileTime(&st, lpFileTime);
}

LONG WINAPI CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2)
{
	if (lpFileTime1->dwHighDateTime != lpFileTime2->dwHighDateTime)
		return lpFileTime1->dwHighDateTime < lpFileTime2->dwHighDateTime ? -1 : 1;
	if (lpFileTime1->dwLowDateTime != lpFileTime2->dwLowDateTime)
		return lpFileTime1->dwLowDateTime < lpFileTime2->dwLowDateTime ? -1 : 1;
	return 0;
}

DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation)
{
	memset(lpTimeZoneInformation, 0, sizeof(*lpTimeZoneInformation));
	time_t now = time(nullptr);
	struct tm tm;
	localtime_r(&now, &tm);
	// Bias is minutes west of UTC, excluding daylight saving time.
	const LONG offsetMinutes = (LONG)(tm.tm_gmtoff / 60);
	lpTimeZoneInformation->Bias = tm.tm_isdst > 0 ? -(offsetMinutes - 60) : -offsetMinutes;
	lpTimeZoneInformation->DaylightBias = -60;
	return tm.tm_isdst > 0 ? 2 : 1; // TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD
}

int WINAPI GetDateFormatA(LCID, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCSTR lpFormat, LPSTR lpDateStr, int cchDate)
{
	const char *defaultFormat = (dwFlags & DATE_LONGDATE) ? "dddd, MMMM d, yyyy" : "M/d/yyyy";
	return FormatDateTime(lpDate, lpFormat, defaultFormat, lpDateStr, cchDate);
}

int WINAPI GetTimeFormatA(LCID, DWORD dwFlags, const SYSTEMTIME *lpTime, LPCSTR lpFormat, LPSTR lpTimeStr, int cchTime)
{
	const char *defaultFormat = (dwFlags & TIME_NOSECONDS) ? "h:mm tt" : "h:mm:ss tt";
	return FormatDateTime(lpTime, lpFormat, defaultFormat, lpTimeStr, cchTime);
}

static int FormatWide(LCID locale, DWORD flags, const SYSTEMTIME *st, LPCWSTR format, LPWSTR out, int capacity, bool date)
{
	char narrowFormat[128];
	if (format)
		WidenFormat(format, narrowFormat, sizeof(narrowFormat));
	char narrowOut[256];
	const int length = date ? GetDateFormatA(locale, flags, st, format ? narrowFormat : nullptr, capacity ? narrowOut : nullptr, capacity ? (int)sizeof(narrowOut) : 0)
	                        : GetTimeFormatA(locale, flags, st, format ? narrowFormat : nullptr, capacity ? narrowOut : nullptr, capacity ? (int)sizeof(narrowOut) : 0);
	if (capacity == 0 || length == 0)
		return length;
	if (length > capacity)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	for (int i = 0; i < length; ++i)
		out[i] = (unsigned char)narrowOut[i];
	return length;
}

int WINAPI GetDateFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpDate, LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate)
{
	return FormatWide(Locale, dwFlags, lpDate, lpFormat, lpDateStr, cchDate, true);
}

int WINAPI GetTimeFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME *lpTime, LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime)
{
	return FormatWide(Locale, dwFlags, lpTime, lpFormat, lpTimeStr, cchTime, false);
}

/* -------------------------------------------------------------------------
** Multimedia timers
** ----------------------------------------------------------------------- */

DWORD WINAPI timeGetTime(void)
{
	return GetTickCount();
}

MMRESULT WINAPI timeBeginPeriod(UINT)
{
	return TIMERR_NOERROR;
}

MMRESULT WINAPI timeEndPeriod(UINT)
{
	return TIMERR_NOERROR;
}

MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS ptc, UINT cbtc)
{
	if (!ptc || cbtc < sizeof(TIMECAPS))
		return 11; // MMSYSERR_INVALPARAM
	ptc->wPeriodMin = 1;
	ptc->wPeriodMax = 1000000;
	return TIMERR_NOERROR;
}

} // extern "C"

namespace
{

struct MultimediaTimer
{
	UINT id;
	UINT delay;
	LPTIMECALLBACK callback;
	DWORD_PTR user;
	UINT flags;
	pthread_t thread;
	std::atomic<bool> cancelled;
};

pthread_mutex_t s_timerLock = PTHREAD_MUTEX_INITIALIZER;
MultimediaTimer *s_timers[64];
UINT s_nextTimerId = 1;

void *MultimediaTimerThread(void *arg)
{
	MultimediaTimer *timer = static_cast<MultimediaTimer *>(arg);
	int64_t next = MonotonicNanoseconds() + (int64_t)timer->delay * 1000000;
	while (!timer->cancelled)
	{
		const int64_t now = MonotonicNanoseconds();
		if (now < next)
		{
			const int64_t wait = next - now;
			struct timespec ts = { (time_t)(wait / 1000000000), (long)(wait % 1000000000) };
			nanosleep(&ts, nullptr);
			continue;
		}
		if (timer->cancelled)
			break;
		timer->callback(timer->id, 0, timer->user, 0, 0);
		if (!(timer->flags & TIME_PERIODIC))
			break;
		next += (int64_t)(timer->delay ? timer->delay : 1) * 1000000;
	}
	return nullptr;
}

} // namespace

extern "C" {

MMRESULT WINAPI timeSetEvent(UINT uDelay, UINT, LPTIMECALLBACK fptc, DWORD_PTR dwUser, UINT fuEvent)
{
	if (!fptc)
		return 0;
	MultimediaTimer *timer = new MultimediaTimer();
	timer->delay = uDelay;
	timer->callback = fptc;
	timer->user = dwUser;
	timer->flags = fuEvent;
	timer->cancelled = false;

	pthread_mutex_lock(&s_timerLock);
	int slot = -1;
	for (int i = 0; i < 64; ++i)
	{
		if (!s_timers[i])
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
	{
		pthread_mutex_unlock(&s_timerLock);
		delete timer;
		return 0;
	}
	timer->id = s_nextTimerId++;
	s_timers[slot] = timer;
	pthread_mutex_unlock(&s_timerLock);

	if (pthread_create(&timer->thread, nullptr, MultimediaTimerThread, timer) != 0)
	{
		pthread_mutex_lock(&s_timerLock);
		s_timers[slot] = nullptr;
		pthread_mutex_unlock(&s_timerLock);
		delete timer;
		return 0;
	}
	return timer->id;
}

MMRESULT WINAPI timeKillEvent(UINT uTimerID)
{
	MultimediaTimer *timer = nullptr;
	pthread_mutex_lock(&s_timerLock);
	for (int i = 0; i < 64; ++i)
	{
		if (s_timers[i] && s_timers[i]->id == uTimerID)
		{
			timer = s_timers[i];
			s_timers[i] = nullptr;
			break;
		}
	}
	pthread_mutex_unlock(&s_timerLock);
	if (!timer)
		return 97; // MMSYSERR_INVALPARAM-like: TIMERR_NOCANDO
	timer->cancelled = true;
	if (pthread_equal(timer->thread, pthread_self()))
	{
		// Killed from its own callback: the thread ends by itself.
		pthread_detach(timer->thread);
		return TIMERR_NOERROR;
	}
	pthread_join(timer->thread, nullptr);
	delete timer;
	return TIMERR_NOERROR;
}

} // extern "C"
