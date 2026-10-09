// Shared by the WebCompat test sources.
#pragma once

#include <stdio.h>

extern int g_checks;
extern int g_failures;

#define CHECK(condition) \
	do { \
		++g_checks; \
		if (!(condition)) { \
			++g_failures; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)
