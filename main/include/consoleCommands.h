
// The console command interface is generally used only by console.c, 
// if you want to add a command, go to consoleCommands.c

#ifndef CONSOLE_COMMANDS_H
#define CONSOLE_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "console.h"

#define CONSOLE_COMMAND_MAX_COMMAND_LENGTH 10       // command only
#define CONSOLE_COMMAND_MAX_LENGTH 256              // whole command with argument
#define CONSOLE_COMMAND_HAS_HELP                    // if not defined, commands have no help

#if defined(CONSOLE_COMMAND_HAS_HELP)
    #define HELP(x)  (x)
#else
    #define HELP(x)	  0
#endif // CONSOLE_COMMAND_HAS_HELP

typedef eCommandResult_T(*ConsoleCommand_T)(const char buffer[]);

typedef struct sConsoleCommandStruct
{
    char const * const name;
    ConsoleCommand_T execute;
#if defined(CONSOLE_COMMAND_HAS_HELP)
    char const * const help;
#else
    uint8_t junk;
#endif // CONSOLE_COMMAND_HAS_HELP 
} sConsoleCommandTable_T;

#define CONSOLE_COMMAND_TABLE_END {NULL,NULL, HELP("")}

const sConsoleCommandTable_T* ConsoleCommandsGetTable(void);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_COMMANDS_H
