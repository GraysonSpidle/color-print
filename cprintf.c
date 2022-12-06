/* Written to this specification: https://cplusplus.com/reference/cstdio/printf/
* Date accessed: November 30 2022
*
* Contains definitions for cprintf and cwprintf.
*/

#include "cprintf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <minwindef.h>

// init externs from the .h file

CONSOLE_SCREEN_BUFFER_INFO cprintf_previous_screen_buffer_info;
bool cprintf_set_previous = false;

/* So this is the macros section...
* In an attempt to make code that didn't result in having to change things
* in multiple places, I've found a way to genericize my functions with macros!
* At what cost? Readability. Sorry in advance. I've tried to remedy this by
* being generous with the comments, but it's putting lipstick on a pig.
*/

#if defined(CPRINTF_BUF_SIZE)
#error Macro clash!
#endif
// the buffer size (in chars or wchars) for the escaped sequence like %s or %0.2f
#define CPRINTF_BUF_SIZE 20

#if defined(CPRINTF_TEXT)
#error Macro clash!
#endif
#define CPRINTF_TEXT(dtype, s) _Generic((dtype), \
	wchar_t: L ## s, \
	char: s, \
	default: s \
)

#if defined(CPRINTF_STRCHR)
#error Macro clash!
#endif
#define CPRINTF_STRCHR(dtype, str, c) _Generic((dtype), \
	wchar_t: wcschr(str, L ## c), \
	char: strchr(str, c), \
	default: strchr(str, c) \
)

#if defined(CPRINTF_ATOI)
#error Macro clash!
#endif
#define CPRINTF_ATOI(dtype, str) _Generic((dtype), \
	wchar_t: wcstol(str, NULL, 0), \
	char: atoi(str), \
	default: atoi(str) \
)

#if defined(CPRINTF_PUTCHAR)
#error Macro clash!
#endif
#define CPRINTF_PUTCHAR(dtype, c) _Generic((dtype), \
	wchar_t: putwchar(c), \
	char: putchar(c), \
	default: putchar(c) \
)

#if defined(CPRINTF_FUNC_SWITCH)
#error Macro clash!
#endif
#define CPRINTF_FUNC_SWITCH(dtype, func, ...) _Generic((dtype), \
	wchar_t: w ## func, \
	char: ## func, \
	default: ## func \
)(__VA_ARGS__)

/* The way colors work in Windows is... interesting. You add red, green, or blue to the color you want the background
* or foreground to be. Additionally, you have the option to make the colors bright.
* Think of this in terms of adding values to RGB. Adding red (not making it bright) adds 127 to R, but making it bright
* adds 255 to R. This logic works until you reach black and white. Black, when bright, makes gray. White, when not bright,
* makes a lighter gray.
* 
* Anyway, I hope this helps understand what's going on here.
*/

void apply_background(WORD* pAttrs, int attr) {
	*pAttrs |= BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
	switch (attr) {
		case 40: // black
			*pAttrs ^= BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE; // leave nothing
			break;
		case 41: // red
			*pAttrs ^= BACKGROUND_GREEN | BACKGROUND_BLUE; // red remains
			break;
		case 42: // green
			*pAttrs ^= BACKGROUND_RED | BACKGROUND_BLUE; // green remains
			break;
		case 43: // yellow
			*pAttrs ^= BACKGROUND_BLUE; // red and green remain
			break;
		case 44: // blue
			*pAttrs ^= BACKGROUND_RED | BACKGROUND_GREEN; // blue remains
			break;
		case 45: // magenta
			*pAttrs ^= BACKGROUND_GREEN; // red and blue remain
			break;
		case 46: // cyan
			*pAttrs ^= BACKGROUND_RED; // green and blue remain
			break;
		case 47: // white
			// do nothing
			break;
		default:
			// error
			break;
	}
}

void apply_foreground(WORD* pAttrs, int attr) {
	*pAttrs |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	switch (attr) {
		case 30: // black
			*pAttrs ^= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // leave nothing
			break;
		case 31: // red
			*pAttrs ^= FOREGROUND_GREEN | FOREGROUND_BLUE; // red remains
			break;
		case 32: // green
			*pAttrs ^= FOREGROUND_RED | FOREGROUND_BLUE; // green remains
			break;
		case 33: // yellow
			*pAttrs ^= FOREGROUND_BLUE; // red and green remain
			break;
		case 34: // blue
			*pAttrs ^= FOREGROUND_RED | FOREGROUND_GREEN; // blue remains
			break;
		case 35: // magenta
			*pAttrs ^= FOREGROUND_GREEN; // red and blue remain
			break;
		case 36: // cyan
			*pAttrs ^= FOREGROUND_RED; // green and blue remain
			break;
		case 37: // white
			// do nothing
			break;
		default:
			// error
			break;
	}
}

void apply_special(WORD* pAttrs, int attr) {
	switch (attr) {
		case 0: // reset
			*pAttrs = -1;
			break;
		case 1: // bold (intensity)
			// only foreground
			*pAttrs |= FOREGROUND_INTENSITY;
			break;
		case 21: // bold off (intensity)
			*pAttrs |= FOREGROUND_INTENSITY;
			*pAttrs ^= FOREGROUND_INTENSITY;
			break;
		case 7: // inverse
			*pAttrs |= COMMON_LVB_REVERSE_VIDEO;
			break;
		case 27: // inverse off
			*pAttrs |= COMMON_LVB_REVERSE_VIDEO;
			*pAttrs ^= COMMON_LVB_REVERSE_VIDEO;
			break;
		case 4: // underline
			*pAttrs |= COMMON_LVB_UNDERSCORE;
			break;
		case 24: // underline off
			*pAttrs |= COMMON_LVB_UNDERSCORE;
			*pAttrs ^= COMMON_LVB_UNDERSCORE;
			break;
		default:
			// error
			break;
	}
}

void parse_color_sequence(const char* ptr, WORD* pAttributes) {
	// IF YOU COPY AND PASTE THIS FUNCTION FOR EITHER parse_color_sequence OR wparse_color_sequence
	// THEN CHANGE THESE ACCORDINGLY
	typedef char char_t;
	char dtype_check; // for macros that need to know the dtype
	// ============================================================================================
	if (!ptr || !pAttributes)
		return;

	// these ptrs will hold the place in the string for chars we're searching for

	const char_t* semi_ptr = NULL;
	const char_t* m_ptr = NULL;

	char_t buf[3]; // for atoi()

	CONSOLE_SCREEN_BUFFER_INFO info;
	if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		// TODO: error
		printf("%s error\n", __FUNCTION__);
		return;
	}

	if (ptr[1] != CPRINTF_TEXT(dtype_check, '['))
		return;

	ptr += 2;
	semi_ptr = CPRINTF_STRCHR(dtype_check, ptr, ';');
	m_ptr = CPRINTF_STRCHR(dtype_check, ptr, 'm');

	if (!m_ptr) {
		// TODO: error
		return;
	}

	*pAttributes = info.wAttributes;

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ====================
	// parsing special arg
	// ====================

	if (ptr != semi_ptr) {
		if (semi_ptr && semi_ptr < m_ptr)
			memcpy(buf, ptr, min(semi_ptr - ptr, 2) * sizeof(dtype_check));
		else
			memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_special(pAttributes, CPRINTF_ATOI(dtype_check, buf)); // buf is zero terminated

		if (*pAttributes == (WORD) -1) { // means reset 
			*pAttributes = cprintf_previous_screen_buffer_info.wAttributes;
			return;
		}
	}

	if (!semi_ptr)
		return;
	ptr = semi_ptr + 1;
	if (ptr == m_ptr) // No more args left
		return;
	semi_ptr = CPRINTF_STRCHR(dtype_check, ptr, ';'); // find another semicolon

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ======================
	// parsing foreground arg
	// ======================

	if (ptr != semi_ptr) {
		if (semi_ptr && semi_ptr < m_ptr)
			memcpy(buf, ptr, min(semi_ptr - ptr, 2) * sizeof(dtype_check));
		else
			memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_foreground(pAttributes, CPRINTF_ATOI(dtype_check, buf));
	}

	if (!semi_ptr)
		return;
	ptr = semi_ptr + 1;
	if (ptr == m_ptr)
		return;

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ======================
	// parsing background arg
	// ======================
	if (ptr != m_ptr) {
		memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_background(pAttributes, CPRINTF_ATOI(dtype_check, buf));
	}

	ptr = m_ptr;
}
void wparse_color_sequence(const wchar_t* ptr, WORD* pAttributes) {
	typedef wchar_t char_t;
	wchar_t dtype_check; // for macros that need to know the dtype
	if (!ptr || !pAttributes)
		return;

	// these ptrs will hold the place in the string for chars we're searching for

	const char_t* semi_ptr = NULL;
	const char_t* m_ptr = NULL;

	char_t buf[3]; // for atoi()

	CONSOLE_SCREEN_BUFFER_INFO info;
	if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
		// TODO: error
		printf("parse_color_sequence() error\n");
		return;
	}

	if (ptr[1] != CPRINTF_TEXT(dtype_check, '['))
		return;

	ptr += 2;
	semi_ptr = CPRINTF_STRCHR(dtype_check, ptr, ';');
	m_ptr = CPRINTF_STRCHR(dtype_check, ptr, 'm');

	if (!m_ptr) {
		// TODO: error
		return;
	}

	*pAttributes = info.wAttributes;

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ====================
	// parsing special arg
	// ====================

	if (ptr != semi_ptr) {
		if (semi_ptr && semi_ptr < m_ptr)
			memcpy(buf, ptr, min(semi_ptr - ptr, 2) * sizeof(dtype_check));
		else
			memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_special(pAttributes, CPRINTF_ATOI(dtype_check, buf)); // buf is zero terminated

		if (*pAttributes == (WORD) -1) { // means reset 
			*pAttributes = cprintf_previous_screen_buffer_info.wAttributes;
			return;
		}
	}

	if (!semi_ptr)
		return;
	ptr = semi_ptr + 1;
	if (ptr == m_ptr) // No more args left
		return;
	semi_ptr = CPRINTF_STRCHR(dtype_check, ptr, ';'); // find another semicolon

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ======================
	// parsing foreground arg
	// ======================

	if (ptr != semi_ptr) {
		if (semi_ptr && semi_ptr < m_ptr)
			memcpy(buf, ptr, min(semi_ptr - ptr, 2) * sizeof(dtype_check));
		else
			memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_foreground(pAttributes, CPRINTF_ATOI(dtype_check, buf));
	}

	if (!semi_ptr)
		return;
	ptr = semi_ptr + 1;
	if (ptr == m_ptr)
		return;

	memset(buf, 0, 3 * sizeof(dtype_check));

	// ======================
	// parsing background arg
	// ======================
	if (ptr != m_ptr) {
		memcpy(buf, ptr, min(m_ptr - ptr, 2) * sizeof(dtype_check));
		apply_background(pAttributes, CPRINTF_ATOI(dtype_check, buf));
	}

	ptr = m_ptr;
}

const char* find_any(const char* cstr, const char* chars) {
	for (const char* ptr = cstr; *ptr != '\0'; ++ptr) {
		for (const char* chars_ptr = chars; *chars_ptr != '\0'; ++chars_ptr) {
			if (*ptr == *chars_ptr)
				return ptr;
		}
	}
	return NULL;
}
const wchar_t* wfind_any(const wchar_t* cstr, const wchar_t* chars) {
	for (const wchar_t* ptr = cstr; *ptr != L'\0'; ++ptr) {
		for (const wchar_t* chars_ptr = chars; *chars_ptr != L'\0'; ++chars_ptr) {
			if (*ptr == *chars_ptr)
				return ptr;
		}
	}
	return NULL;
}

int cprintf(const char* const format, ...) {
	if (!cprintf_set_previous) {
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cprintf_previous_screen_buffer_info);
		cprintf_set_previous = true;
	}
	// IF YOU COPY AND PASTE THIS FUNCTION FOR EITHER cprintf OR cwprintf
	// THEN CHANGE THESE ACCORDINGLY
	char dtype;
	typedef char char_t;
	// ==================================================================

	int chars_written = 0;
	int res;

	va_list arg;
	va_start(arg, format);

	WORD attrs;

	const char_t* pos = NULL;
	const char_t* length_pos = NULL;
	char_t buf[CPRINTF_BUF_SIZE];

	for (const char_t* ptr = format; *ptr != CPRINTF_TEXT(dtype, '\0'); ++ptr) {
		// Iterating through the string until we find a % or a null character
		// we are also printing the chars that don't meet those criteria
		while (*ptr != CPRINTF_TEXT(dtype, '%') && *ptr != CPRINTF_TEXT(dtype, '\0')) {
			CPRINTF_PUTCHAR(dtype, *ptr);
			ptr++;
			chars_written++;
		}

		// If we at the end of the string, then break
		if (*ptr == CPRINTF_TEXT(dtype, '\0'))
			break;
	
		// check if the % isn't an escaped %
		if (ptr[1] == CPRINTF_TEXT(dtype, '%')) {
			++ptr;
			continue;
		}

		// If we get here, then we have come across a printf escape sequence

		memset(buf, 0, CPRINTF_BUF_SIZE * sizeof(char_t)); // zero buffer (so it is always null terminated)
		
		// Find any character in that string. These characters are the "ending characters"
		pos = CPRINTF_FUNC_SWITCH(dtype, find_any, ptr, CPRINTF_TEXT(dtype, "diuoxXfFeEgGaAcspn m"));
		if (!pos) // if we didn't find one
			continue;
		memcpy(buf, ptr, min(1 + pos - ptr, CPRINTF_BUF_SIZE - 1) * sizeof(char_t)); // copy the string into the buffer but truncate it at CPRINTF_BUF_SIZE - 1

		// finding length specifier
		length_pos = CPRINTF_FUNC_SWITCH(dtype, find_any, ptr, CPRINTF_TEXT(dtype, "hljztL"));

		// Now we decipher what the user wants to do
		res = 0;
		switch (*pos) {
			case CPRINTF_TEXT(dtype, 'd'): // signed decimal integer
			case CPRINTF_TEXT(dtype, 'i'): // signed decimal integer
				if (!length_pos || length_pos > pos) {
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, short int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, signed char));
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long long int));
						break;
					case CPRINTF_TEXT(dtype, 'j'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, intmax_t));
						break;
					case CPRINTF_TEXT(dtype, 'z'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, size_t));
						break;
					case CPRINTF_TEXT(dtype, 't'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, ptrdiff_t));
						break;
					case CPRINTF_TEXT(dtype, 'L'): // N/A but we will do default
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
						break;
					default:
						break;
				}
				break;
			case CPRINTF_TEXT(dtype, 'u'): // unsigned decimal integer
			case CPRINTF_TEXT(dtype, 'o'): // unsigned octal
			case CPRINTF_TEXT(dtype, 'x'): // unsigned hexadecimal integer
			case CPRINTF_TEXT(dtype, 'X'): // unsigned hexadecimal integer (uppercase)
				if (!length_pos || length_pos > pos) {
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned int));
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned short int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned char));
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned long int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned long long int));
						break;
					case CPRINTF_TEXT(dtype, 'j'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, uintmax_t));
						break;
					case CPRINTF_TEXT(dtype, 'z'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, size_t));
						break;
					case CPRINTF_TEXT(dtype, 't'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, ptrdiff_t));
						break;
					case CPRINTF_TEXT(dtype, 'L'): // N/A but we will do default
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned int));
						break;
					default:
						break;
				}
				break;
			case CPRINTF_TEXT(dtype, 'f'): // decimal floating point
			case CPRINTF_TEXT(dtype, 'F'): // decimal floating point (uppercase)
			case CPRINTF_TEXT(dtype, 'e'): // scientific notation (mantissa/exponent)
			case CPRINTF_TEXT(dtype, 'E'): // scientific notation (mantissa/exponent) (uppercase)
			case CPRINTF_TEXT(dtype, 'g'): // Use the shortest representation: %e or %f
			case CPRINTF_TEXT(dtype, 'G'): // Use the shortest representation: %E or %F
			case CPRINTF_TEXT(dtype, 'a'): // hexadecimal floating point
			case CPRINTF_TEXT(dtype, 'A'): // Hexadecimal floating point (uppercase)
				if (!length_pos || length_pos > pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, double));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'L'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long double));
				break;
			case CPRINTF_TEXT(dtype, 'c'): // character
				if (!length_pos || length_pos > pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'l'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, wint_t));
				break;
			case CPRINTF_TEXT(dtype, 's'): // string of characters
				if (!length_pos || length_pos > pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, char*));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'l'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, wchar_t*));
				break;
			case CPRINTF_TEXT(dtype, 'p'): // pointer address
				res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, void*));
				break;
			case CPRINTF_TEXT(dtype, 'n'): // number of characters written so far
				if (!length_pos || length_pos > pos) {
					int* p = va_arg(arg, int*);
					*p = (int) chars_written;
					res = 0;
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0]) {
							short int* p = va_arg(arg, short int*);
							*p = (short int) chars_written;
						}
						else {
							signed char* p = va_arg(arg, signed char*);
							*p = (signed char) chars_written;
						}
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0]) {
							long int* p = va_arg(arg, long int*);
							*p = (long int) chars_written;
						}
						else {
							long long int* p = va_arg(arg, long long int*);
							*p = (long long int) chars_written;
						}
						break;
					case CPRINTF_TEXT(dtype, 'j'): {
						intmax_t* p = va_arg(arg, intmax_t*);
						*p = (intmax_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 'z'): {
						size_t* p = va_arg(arg, size_t*);
						*p = (size_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 't'): {
						ptrdiff_t* p = va_arg(arg, ptrdiff_t*);
						*p = (ptrdiff_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 'L'): // N/A
					default:
						break;
				}
				res = 0;
				break;
			case CPRINTF_TEXT(dtype, 'm'): { // our color escape sequence
				if (ptr[1] != CPRINTF_TEXT(dtype, '[')) // not the escape character we're looking for
					break;
				CPRINTF_FUNC_SWITCH(dtype, parse_color_sequence, ptr, &attrs); // parse the color sequence

				if (!SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attrs)) {
					// TODO: error
					return -1;
				}
				res = 0;
				break;
			}
			case CPRINTF_TEXT(dtype, '%'): // escaped the sequence
				res = CPRINTF_PUTCHAR(dtype, *pos);
				break;
			case CPRINTF_TEXT(dtype, ' '): // didn't finish the sequence
				// TODO: ?
			default:
				break;
		}
		ptr = pos;
		if (res >= 0)
			chars_written += res;
		else
			return res;
	}
	va_end(arg);
	return (int) chars_written;
}
int cwprintf(const wchar_t* const format, ...) {
	if (!cprintf_set_previous) {
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cprintf_previous_screen_buffer_info);
		cprintf_set_previous = true;
	}
	// IF YOU COPY AND PASTE THIS FUNCTION FOR EITHER cprintf OR cwprintf
	// THEN CHANGE THESE ACCORDINGLY
	wchar_t dtype;
	typedef wchar_t char_t;
	// ==================================================================

	int chars_written = 0;
	int res;

	va_list arg;
	va_start(arg, format);

	WORD attrs;

	const char_t* pos = NULL;
	const char_t* length_pos = NULL;
	char_t buf[CPRINTF_BUF_SIZE];

	for (const char_t* ptr = format; *ptr != CPRINTF_TEXT(dtype, '\0'); ++ptr) {
		// Iterating through the string until we find a % or a null character
		// we are also printing the chars that don't meet those criteria
		while (*ptr != CPRINTF_TEXT(dtype, '%') && *ptr != CPRINTF_TEXT(dtype, '\0')) {
			CPRINTF_PUTCHAR(dtype, *ptr);
			ptr++;
			chars_written++;
		}

		// If we at the end of the string, then break
		if (*ptr == CPRINTF_TEXT(dtype, '\0'))
			break;

		// check if the % isn't an escaped %
		if (ptr[1] == CPRINTF_TEXT(dtype, '%')) {
			++ptr;
			continue;
		}

		// If we get here, then we have come across a printf escape sequence

		memset(buf, 0, CPRINTF_BUF_SIZE * sizeof(char_t)); // zero buffer (so it is always null terminated)

														   // Find any character in that string. These characters are the "ending characters"
		pos = CPRINTF_FUNC_SWITCH(dtype, find_any, ptr, CPRINTF_TEXT(dtype, "diuoxXfFeEgGaAcspn m"));
		if (!pos) // if we didn't find one
			continue;
		memcpy(buf, ptr, min(1 + pos - ptr, CPRINTF_BUF_SIZE - 1) * sizeof(char_t)); // copy the string into the buffer but truncate it at CPRINTF_BUF_SIZE - 1

																					 // finding length specifier
		length_pos = CPRINTF_FUNC_SWITCH(dtype, find_any, ptr, CPRINTF_TEXT(dtype, "hljztL"));

		// Now we decipher what the user wants to do
		res = 0;
		switch (*pos) {
			case CPRINTF_TEXT(dtype, 'd'): // signed decimal integer
			case CPRINTF_TEXT(dtype, 'i'): // signed decimal integer
				if (!length_pos) {
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, short int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, signed char));
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long long int));
						break;
					case CPRINTF_TEXT(dtype, 'j'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, intmax_t));
						break;
					case CPRINTF_TEXT(dtype, 'z'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, size_t));
						break;
					case CPRINTF_TEXT(dtype, 't'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, ptrdiff_t));
						break;
					case CPRINTF_TEXT(dtype, 'L'): // N/A but we will do default
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
						break;
					default:
						break;
				}
				break;
			case CPRINTF_TEXT(dtype, 'u'): // unsigned decimal integer
			case CPRINTF_TEXT(dtype, 'o'): // unsigned octal
			case CPRINTF_TEXT(dtype, 'x'): // unsigned hexadecimal integer
			case CPRINTF_TEXT(dtype, 'X'): // unsigned hexadecimal integer (uppercase)
				if (!length_pos) {
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned int));
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned short int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned char));
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0])
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned long int));
						else
							res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned long long int));
						break;
					case CPRINTF_TEXT(dtype, 'j'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, uintmax_t));
						break;
					case CPRINTF_TEXT(dtype, 'z'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, size_t));
						break;
					case CPRINTF_TEXT(dtype, 't'):
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, ptrdiff_t));
						break;
					case CPRINTF_TEXT(dtype, 'L'): // N/A but we will do default
						res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, unsigned int));
						break;
					default:
						break;
				}
				break;
			case CPRINTF_TEXT(dtype, 'f'): // decimal floating point
			case CPRINTF_TEXT(dtype, 'F'): // decimal floating point (uppercase)
			case CPRINTF_TEXT(dtype, 'e'): // scientific notation (mantissa/exponent)
			case CPRINTF_TEXT(dtype, 'E'): // scientific notation (mantissa/exponent) (uppercase)
			case CPRINTF_TEXT(dtype, 'g'): // Use the shortest representation: %e or %f
			case CPRINTF_TEXT(dtype, 'G'): // Use the shortest representation: %E or %F
			case CPRINTF_TEXT(dtype, 'a'): // hexadecimal floating point
			case CPRINTF_TEXT(dtype, 'A'): // Hexadecimal floating point (uppercase)
				if (!length_pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, double));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'L'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, long double));
				break;
			case CPRINTF_TEXT(dtype, 'c'): // character
				if (!length_pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, int));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'l'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, wint_t));
				break;
			case CPRINTF_TEXT(dtype, 's'): // string of characters
				if (!length_pos)
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, char*));
				else if (*length_pos == CPRINTF_TEXT(dtype, 'l'))
					res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, wchar_t*));
				break;
			case CPRINTF_TEXT(dtype, 'p'): // pointer address
				res = CPRINTF_FUNC_SWITCH(dtype, printf, buf, va_arg(arg, void*));
				break;
			case CPRINTF_TEXT(dtype, 'n'): // number of characters written so far
				if (!length_pos) {
					int* p = va_arg(arg, int*);
					*p = (int) chars_written;
					res = 0;
					break;
				}
				// if there is a length specified, then we have to cast the argument we got appropriately
				switch (*length_pos) {
					case CPRINTF_TEXT(dtype, 'h'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0]) {
							short int* p = va_arg(arg, short int*);
							*p = (short int) chars_written;
						}
						else {
							signed char* p = va_arg(arg, signed char*);
							*p = (signed char) chars_written;
						}
						break;
					case CPRINTF_TEXT(dtype, 'l'):
						// check if there's not another one
						if (length_pos[1] != length_pos[0]) {
							long int* p = va_arg(arg, long int*);
							*p = (long int) chars_written;
						}
						else {
							long long int* p = va_arg(arg, long long int*);
							*p = (long long int) chars_written;
						}
						break;
					case CPRINTF_TEXT(dtype, 'j'): {
						intmax_t* p = va_arg(arg, intmax_t*);
						*p = (intmax_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 'z'): {
						size_t* p = va_arg(arg, size_t*);
						*p = (size_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 't'): {
						ptrdiff_t* p = va_arg(arg, ptrdiff_t*);
						*p = (ptrdiff_t) chars_written;
						break;
					}
					case CPRINTF_TEXT(dtype, 'L'): // N/A
					default:
						break;
				}
				res = 0;
				break;
			case CPRINTF_TEXT(dtype, 'm'): { // our color escape sequence
				if (ptr[1] != CPRINTF_TEXT(dtype, '[')) // not the escape character we're looking for
					break;
				CPRINTF_FUNC_SWITCH(dtype, parse_color_sequence, ptr, &attrs); // parse the color sequence

				if (!SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attrs)) {
					// TODO: error
					return -1;
				}
				res = 0;
				break;
			}
			case CPRINTF_TEXT(dtype, '%'): // escaped the sequence
				res = CPRINTF_PUTCHAR(dtype, *pos);
				break;
			case CPRINTF_TEXT(dtype, ' '): // didn't finish the sequence
										   // TODO: ?
			default:
				break;
		}
		ptr = pos;
		if (res >= 0)
			chars_written += res;
		else
			return res;
	}
	va_end(arg);
	return (int) chars_written;
}