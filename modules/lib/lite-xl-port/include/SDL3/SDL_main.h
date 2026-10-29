#pragma once

/* As in real SDL, the program's main() is renamed so the platform layer can wrap it:
   lib/lite-xl-port/main.c connects to StrawX and sets up the environment first. */

#define main lite_xl_main
