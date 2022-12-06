# color-print
This is a fun little exercise I went through to see a glimpse into what C is all about. Coming from C++, this wasn't too challenging, but certainly wasn't a cake-walk.
Overall, I enjoyed my time doing this exercise.

The goal was to replicate Linux's text coloring system for Windows in the form of `printf`.
I wanted the final product to be identical to the original, and was slightly disappointed with the result.

In the Linux version, the escape sequence starts with `\e` which I couldn't get C to read that character so I compromised and went with `%`.
Additionally, the underline feature doesn't work. I don't know if I made a stupid mistake or what, but it just doesn't work. I'm not going to
waste my life trying to fix it.

Anyway, I'm done with this project. I'm posting this on here for if *maybe* someone needs something like this for reference, or for someone experienced who is looking for some laughs.

# Requirements
- C11 Language Standard
- Windows OS (duh)

# Example
```c
#include <stdio.h>
#include "cprintf.h"

int main(void) {
    cprintf("%[21;30mThis is black!\n"); // this should appear as a blank line (since the background is also black)
	cprintf("%[21;31mThis is red!\n");
	cprintf("%[21;32mThis is green!\n");
	cprintf("%[21;33mThis is yellow!\n");
	cprintf("%[21;34mThis is blue!\n");
	cprintf("%[21;35mThis is magenta!\n");
	cprintf("%[21;36mThis is cyan!\n");
	cprintf("%[21;37mThis is white!\n\n");

	cprintf("%[1;30mThis is bolded black!\n");
	cprintf("%[1;31mThis is bolded red!\n");
	cprintf("%[1;32mThis is bolded green!\n");
	cprintf("%[1;33mThis is bolded yellow!\n");
	cprintf("%[1;34mThis is bolded blue!\n");
	cprintf("%[1;35mThis is bolded magenta!\n");
	cprintf("%[1;36mThis is bolded cyan!\n");
	cprintf("%[1;37mThis is bolded white!%[0m\n");
	cprintf("%[7mThis is inverted!%[0m\n");

    cwprintf(L"This should %s work just as well!\n", L"also");

    return 0;
}
```

# Installation
- You copy the `.h` file into your project's header file directory.
- You copy the `.c` file into your project's source file directory.

Please note: Don't use this for any super serious stuff. This has NOT been tested very much and the error handling is... bad.
