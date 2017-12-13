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
#include <driver/gpio.h>

#define IGNORE_UNUSED_VARIABLE(x)     if ( &x == &x ) {}

static eCommandResult_T ConsoleCommandComment(const char buffer[]);
static eCommandResult_T ConsoleCommandHelp(const char buffer[]);
static eCommandResult_T ConsoleCommandVer(const char buffer[]);
static eCommandResult_T ConsoleCommandIoDir(const char buffer[]);
static eCommandResult_T ConsoleCommandIoSet(const char buffer[]);
static eCommandResult_T ConsoleCommandIoGet(const char buffer[]);

static const sConsoleCommandTable_T mConsoleCommandTable[] =
{
    {";", &ConsoleCommandComment, HELP("Comment! You do need a space after the semicolon.")},
    {"help", &ConsoleCommandHelp, HELP("Lists the commands available.")},
    {"ver", &ConsoleCommandVer, HELP("Get the version string.")},

    {"io_dir", &ConsoleCommandIoDir, HELP("Set direction of GPIO. Params: 0=ioNum, 1=ioDir")},
    {"io_set", &ConsoleCommandIoSet, HELP("Set level of GPIO. Params: 0=ioNum, 1=ioVal")},
    {"io_get", &ConsoleCommandIoGet, HELP("Get level of GPIO. Params: 0=ioNum")},

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

static eCommandResult_T ConsoleCommandIoDir(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    esp_err_t resultSet = ESP_FAIL;
    int16_t ioNum, ioMode;
    char outStr[22];

    result = ConsoleReceiveParamInt16(buffer, 1, &ioNum);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 2, &ioMode);
    if((ioNum < 0) || (ioNum > 40)) result = COMMAND_ERROR;

    if((ioNum >= 34) && (ioNum <= 39) && (ioMode != 0)) result = COMMAND_PARAMETER_ERROR; // 34-39 are input only
    if((ioNum == 20) || (ioNum == 24) || ((ioNum >= 28) && (ioNum <= 31))) result = COMMAND_PARAMETER_ERROR; // 20, 24, 28-31 non-existant

    if(COMMAND_SUCCESS == result) resultSet = gpio_set_direction((gpio_num_t) ioNum, (ioMode == 1) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);

    if((COMMAND_SUCCESS == result) && (ESP_OK == resultSet)) {
        snprintf(outStr, sizeof(outStr) / sizeof(outStr[0]), "GPIO %d dir set to %d", ioNum, (ioMode == 1) ? 1 : 0);
        ConsoleIoSendString(outStr);
    } else {
        if(COMMAND_SUCCESS != result) {
            ConsoleIoSendString("Error parsing parameters.");
        } else {
            ConsoleIoSendString("Error setting dir.");
            result = COMMAND_ERROR;
        }
    }
    ConsoleIoSendString(STR_ENDLINE);

    return result;
}

static eCommandResult_T ConsoleCommandIoSet(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    esp_err_t resultSet = ESP_FAIL;
    int16_t ioNum, ioVal;
    char outStr[18];

    result = ConsoleReceiveParamInt16(buffer, 1, &ioNum);
    if(COMMAND_SUCCESS == result) result = ConsoleReceiveParamInt16(buffer, 2, &ioVal);
    if((ioNum < 0) || (ioNum > 33)) result = COMMAND_PARAMETER_ERROR; // 34-39 are input only
    if((ioNum == 20) || (ioNum == 24) || ((ioNum >= 28) && (ioNum <= 31))) result = COMMAND_PARAMETER_ERROR; // 20, 24, 28-31 non-existant

    if(COMMAND_SUCCESS == result) resultSet = gpio_set_level((gpio_num_t) ioNum, (ioVal == 1) ? 1 : 0);

    if((COMMAND_SUCCESS == result) && (ESP_OK == resultSet)) {
        snprintf(outStr, sizeof(outStr) / sizeof(outStr[0]), "GPIO %d set to %d", ioNum, (ioVal == 1) ? 1 : 0);
        ConsoleIoSendString(outStr);
    } else {
        if(COMMAND_SUCCESS != result) {
            ConsoleIoSendString("Error parsing parameters.");
        } else {
            ConsoleIoSendString("Error setting dir.");
            result = COMMAND_ERROR;
        }
    }
    ConsoleIoSendString(STR_ENDLINE);

    return result;
}

static eCommandResult_T ConsoleCommandIoGet(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    int16_t ioNum;
    int ioVal;
    char outStr[20];

    result = ConsoleReceiveParamInt16(buffer, 1, &ioNum);
    if((ioNum < 0) || (ioNum > 40)) result = COMMAND_PARAMETER_ERROR;
    if((ioNum == 20) || (ioNum == 24) || ((ioNum >= 28) && (ioNum <= 31))) result = COMMAND_PARAMETER_ERROR; // 20, 24, 28-31 non-existant

    if(COMMAND_SUCCESS == result) ioVal = gpio_get_level((gpio_num_t) ioNum);

    if((COMMAND_SUCCESS == result)) {
        snprintf(outStr, sizeof(outStr) / sizeof(outStr[0]), "GPIO %d level is %d", ioNum, ioVal);
        ConsoleIoSendString(outStr);
    } else {
        ConsoleIoSendString("Error parsing parameters.");
    }
    ConsoleIoSendString(STR_ENDLINE);

    return result;
}

const sConsoleCommandTable_T* ConsoleCommandsGetTable(void)
{
    return (mConsoleCommandTable);
}


