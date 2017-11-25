// ConsoleCommands.c
// This is where you add commands:
//		1. Add a protoype
//			static eCommandResult_T ConsoleCommandVer(const char buffer[]);
//		2. Add the command to mConsoleCommandTable
//		    {"ver", &ConsoleCommandVer, HELP("Get the version string")},
//		3. Implement the function, using ConsoleReceiveParam<Type> to get the parameters from the buffer.

#include <string.h>
#include <stdio.h>

#include "consoleCommands.h"
#include "console.h"
#include "consoleIo.h"

#include "version.h"

#define IGNORE_UNUSED_VARIABLE(x)     if ( &x == &x ) {}

static eCommandResult_T ConsoleCommandComment(const char buffer[]);
static eCommandResult_T ConsoleCommandHelp(const char buffer[]);
static eCommandResult_T ConsoleCommandVer(const char buffer[]);

static const sConsoleCommandTable_T mConsoleCommandTable[] =
{
    {";", &ConsoleCommandComment, HELP("Comment! You do need a space after the semicolon.")},
    {"help", &ConsoleCommandHelp, HELP("Lists the commands available.")},
    {"ver", &ConsoleCommandVer, HELP("Get the version string.")},

    {"exit", &ConsoleExit, HELP("Exits the command console.")},
    CONSOLE_COMMAND_TABLE_END // must be LAST
};

static eCommandResult_T ConsoleCommandComment(const char buffer[])
{
    // do nothing
    IGNORE_UNUSED_VARIABLE(buffer);
    return COMMAND_SUCCESS;
}

static eCommandResult_T ConsoleCommandHelp(const char buffer[])
{
    uint32_t i;
    uint32_t tableLength;
    eCommandResult_T result = COMMAND_SUCCESS;

    IGNORE_UNUSED_VARIABLE(buffer);

    tableLength = sizeof(mConsoleCommandTable) / sizeof(mConsoleCommandTable[0]);
    for ( i = 0u ; i < tableLength - 1u ; i++ )
    {
        ConsoleIoSendString(mConsoleCommandTable[i].name);
#if CONSOLE_COMMAND_MAX_HELP_LENGTH > 0
        ConsoleIoSendString(" : ");
        ConsoleIoSendString(mConsoleCommandTable[i].help);
#endif // CONSOLE_COMMAND_MAX_HELP_LENGTH > 0
        ConsoleIoSendString(STR_ENDLINE);
    }
    return result;
}

static eCommandResult_T ConsoleCommandVer(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;

    IGNORE_UNUSED_VARIABLE(buffer);

    ConsoleIoSendString(VERSION_STRING);
    ConsoleIoSendString(STR_ENDLINE);
    return result;
}

#if 0
static eCommandResult_T ConsoleCommandSetTime(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    int resultSet = -1;
    int16_t day, month, year, hour, minute, second;

    result = ConsoleReceiveParamInt16(buffer, 1, &day);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 2, &month);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 3, &year);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 4, &hour);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 5, &minute);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 6, &second);

    if(COMMAND_SUCCESS == result) resultSet = TimeHelpers_SetTime(day, month, year, hour, minute, second);

    if((COMMAND_SUCCESS == result) && (0 == resultSet)) {
        ConsoleIoSendString("New time set.");
    } else {
        if(COMMAND_SUCCESS != result) {
            ConsoleIoSendString("Error parsing time.");
        } else {
            ConsoleIoSendString("Error in specified time.");
            result = COMMAND_ERROR;
        }
    }
    ConsoleIoSendString(STR_ENDLINE);
    
    return result;
}
#endif

const sConsoleCommandTable_T* ConsoleCommandsGetTable(void)
{
    return (mConsoleCommandTable);
}


