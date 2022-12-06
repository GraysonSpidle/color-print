#if defined(_MSC_VER) // only windows
#if !defined(__CPRINTF_H__)
#define __CPRINTF_H__
#include <wchar.h>
#include <windows.h>
#include <wincon.h>
#include <stdbool.h>

extern CONSOLE_SCREEN_BUFFER_INFO cprintf_previous_screen_buffer_info;
extern bool cprintf_set_previous;

int cprintf(const char* const format, ...);
int cwprintf(const wchar_t* const format, ...);

#endif // __CPRINTF_H__
#endif // _MSC_VER