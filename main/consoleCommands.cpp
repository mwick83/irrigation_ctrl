// ConsoleCommands.c
// This is where you add commands:
//		1. Add a protoype
//			static eCommandResult_T ConsoleCommandVer(const char buffer[]);
//		2. Add the command to mConsoleCommandTable
//		    {"ver", &ConsoleCommandVer, HELP("Get the version string")},
//		3. Implement the function, using ConsoleReceiveParam<Type> to get the parameters from the buffer.

#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "driver/gpio.h"

#include "consoleCommands.h"
#include "console.h"
#include "consoleIo.h"

#include "version.h"
#include "timeSystem.h"


#define IGNORE_UNUSED_VARIABLE(x)     if ( &x == &x ) {}

static eCommandResult_T ConsoleCommandComment(const char buffer[]);
static eCommandResult_T ConsoleCommandHelp(const char buffer[]);
static eCommandResult_T ConsoleCommandVer(const char buffer[]);
static eCommandResult_T ConsoleCommandIoDir(const char buffer[]);
static eCommandResult_T ConsoleCommandIoSet(const char buffer[]);
static eCommandResult_T ConsoleCommandIoGet(const char buffer[]);
static eCommandResult_T ConsoleCommandTimeGet(const char buffer[]);
static eCommandResult_T ConsoleCommandTimeSet(const char buffer[]);
static eCommandResult_T ConsoleCommandTimeSntp(const char buffer[]);
static eCommandResult_T ConsoleCommandLog(const char buffer[]);
static eCommandResult_T ConsoleCommandLogLevel(const char buffer[]);

static const sConsoleCommandTable_T mConsoleCommandTable[] =
{
    {";", &ConsoleCommandComment, HELP("Comment! You do need a space after the semicolon.")},
    {"help", &ConsoleCommandHelp, HELP("Lists the commands available.")},
    {"ver", &ConsoleCommandVer, HELP("Get the version string.")},

    {"io_dir", &ConsoleCommandIoDir, HELP("Set direction of GPIO. Params: 0=ioNum, 1=ioDir")},
    {"io_set", &ConsoleCommandIoSet, HELP("Set level of GPIO. Params: 0=ioNum, 1=ioVal")},
    {"io_get", &ConsoleCommandIoGet, HELP("Get level of GPIO. Params: 0=ioNum")},

    {"time_get", &ConsoleCommandTimeGet, HELP("Get the current time.")},
    {"time_set", &ConsoleCommandTimeSet, HELP("Set the current time. Format: DD MM YYYY HH MM SS")},
    {"time_sntp", &ConsoleCommandTimeSntp, HELP("(Re-)request time from SNTP server.")},

    {"log", &ConsoleCommandLog, HELP("Set logging on/off. Param: 0:off,1:on")},
    {"log_level", &ConsoleCommandLogLevel, HELP("Set log level. Param: 0:NONE,1:ERR,2:WARN,3:INFO,4:DEBUG,5:DFLT")},

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
    static char outStr[22];

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
    static char outStr[18];

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
    static char outStr[20];

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

static eCommandResult_T ConsoleCommandTimeGet(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    static char timeStr[21];

    IGNORE_UNUSED_VARIABLE(buffer);

    TimeSystem_GetCurTimeStr(timeStr);
    ConsoleIoSendString(timeStr);
    ConsoleIoSendString(STR_ENDLINE);
    return result;
}

static eCommandResult_T ConsoleCommandTimeSet(const char buffer[])
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

    if(COMMAND_SUCCESS == result) resultSet = TimeSystem_SetTime(day, month, year, hour, minute, second);

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

static eCommandResult_T ConsoleCommandTimeSntp(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;

    TimeSystem_SntpRequest();

    ConsoleIoSendString("Get time via SNTP requested.");
    ConsoleIoSendString(STR_ENDLINE);

    return result;
}

static eCommandResult_T ConsoleCommandLog(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    int16_t onOff;

    result = ConsoleReceiveParamInt16(buffer, 1, &onOff);
    if((onOff < 0) || (onOff > 1)) result = COMMAND_PARAMETER_ERROR;

    if(COMMAND_SUCCESS == result) {
        if(onOff == 0) {
            esp_log_level_set("*", ESP_LOG_WARN);
            ConsoleIoSendString("Logging disabled, i.e. set to WARN.");
        } else {
            esp_log_level_set("*", (esp_log_level_t) CONFIG_LOG_DEFAULT_LEVEL);
            ConsoleIoSendString("Default log level set.");
        }
    }

    if(COMMAND_SUCCESS != result) {
        ConsoleIoSendString("Error parsing parameters.");
    }
    ConsoleIoSendString(STR_ENDLINE);

    return result;
}

static eCommandResult_T ConsoleCommandLogLevel(const char buffer[])
{
    eCommandResult_T result = COMMAND_SUCCESS;
    int16_t level;
    static char outStr[20];

    result = ConsoleReceiveParamInt16(buffer, 1, &level);
    if((level < 0) || (level > 5)) result = COMMAND_PARAMETER_ERROR;

    if(COMMAND_SUCCESS == result) {
        esp_log_level_t setLevel;
        switch(level) {
            case 0:  setLevel = ESP_LOG_NONE; break;
            case 1:  setLevel = ESP_LOG_ERROR; break;
            case 2:  setLevel = ESP_LOG_WARN; break;
            case 3:  setLevel = ESP_LOG_INFO; break;
            case 4:  setLevel = ESP_LOG_DEBUG; break;
            default: setLevel = (esp_log_level_t) CONFIG_LOG_DEFAULT_LEVEL; break;
        }

        esp_log_level_set("*", setLevel);
    }

    if(COMMAND_SUCCESS == result) {
        snprintf(outStr, sizeof(outStr) / sizeof(outStr[0]), "Log level set to %d.", level);
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
