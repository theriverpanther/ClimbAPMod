#include <Unreal/UObject.hpp>
#include <cstring>
#include <Windows.h>

#include "ModConsole.hpp"
#include "Archipelago.h"
#include "Helpers/String.hpp"


#define OPTION_MAX_LENGTH 50
#define NB_MAX_OPTIONS 5
#define ALL_OPTIONS_MAX_LENGTH (NB_MAX_OPTIONS * (OPTION_MAX_LENGTH + 1))

#define UNDEFINED_COMMAND -1
#define MAX_OPTIONS_REACHED -2
#define OPTION_LENGTH_REACHED -3

#define CHAMP_ID_START 1
#define FUSE_ID_START 19
#define ABILITY_ID_START 100

using namespace std;
using namespace RC;
using namespace RC::Unreal;

ReceivedItems receivedItems;
TArray<int64_t> pendingItemIDs;

/**
*   @param command: The input command to compare and parse
*   @param expectedCmd: The expected command name to compare with the input command
*   @param outOptions: An out array copying the parsed options separated with '\0'
*   @param outOptionPositions: An out array with the positions of all options in outOptions (-1 for positions without option)
*   @note If an option exceeds OPTION_MAX_LENGTH char or if more options than NB_MAX_OPTIONS are found, -1 is returned
*   @note Get an option with getOptionAtindex()
*
*   @return The number of parsed options, in error case:
*               UNDEFINED_COMMAND if the command is not recognized
*               MAX_OPTIONS_REACHED if the number of input options exceeds NB_MAX_OPTIONS
*               OPTION_LENGTH_REACHED if an option exceeds OPTION_MAX_LENGTH char ('\0 included')
*/
static int CompareAndParseCmd(const char* command, const char* expectedCmd, char outOptions[ALL_OPTIONS_MAX_LENGTH], int outOptionPositions[NB_MAX_OPTIONS])
{
	const char separator = ' ';
	int i = 0, j = 0;
	int numberOfParsedOptions = 0;

	// reset outOptions and outOptionPositions for security
	memset(outOptions, '\0', ALL_OPTIONS_MAX_LENGTH * sizeof(char));
	memset(outOptionPositions, 0, NB_MAX_OPTIONS * sizeof(int)); 

	// Check if the first word of the command is known
	while (expectedCmd[i] != '\0')
	{
		// If a char is different, the command isn't recognized
		if (command[i] != expectedCmd[i])
		{
			return UNDEFINED_COMMAND;
		}
		i++;
	}

	// While the end hasn't been reached, parse options
	while (command[i] != '/n' && command[i] != '\0')
	{
		outOptionsPositions[numberOfParsedOptions] = j;
		int optionLength = 1; // Count the number chars in an option to not exceed max length

		// Ignore trailing separators
		while (command[i] == separator)
		{
			i++;
		}

		// Error if the number expected has been reached without the end of the command line
		if (numberOfParsedOptions >= NB_MAX_OPTIONS && command[i] != '\n' && command[i] != '\0')
		{
			if (optionLength >= OPTION_MAX_LENGTH)
			{
				return OPTION_LENGTH_REACHED;
			}

			outOptions[j] = command[i];
			i++;
			j++;
			optionLength++;
		}

		outOptions[j] = '\0';
		j++;
		numberOfParsedOptions++;
	}

	for (int k = numberOfParsedOptions; k < NB_MAX_OPTIONS; k++)
	{
		outOptionPositions[numberOfParsedOptions] = -1;
	}

	return numberOfParsedOptions;
}

/**
*   @param options: An array with the options separated with '\0'
*   @param optionPositions: An array with the positions of all options in outOptions
*   @param index: The index of the option (should not exceed NB_MAX_OPTIONS)
*   @note CompareAndParseCmd() should be called once before using this function
*
*   @return The option at the provided index, NULL otherwise (incorrect index or no option at this index)
*/
static char* GetOptionAtindex(char options[ALL_OPTIONS_MAX_LENGTH], const int optionPositions[NB_MAX_OPTIONS], const unsigned int index)
{
	if (index >= NB_MAX_OPTIONS || optionPositions[index] == -1)
	{
		return NULL;
	}

	return options + optionPositions[index];
}

/**
*   @brief Receive an item from any world
*   @param itemID: The ID of the received item
*   @param notifyPlayer: Id true, notify the player about the received item, false otherwise
*/
static void ItemReceivedCallback(int64_t itemID, bool notifyPlayer)
{
	if (itemID < 19)
		receivedItems.champs[itemID - 1].amount++;
	else if (itemID < 25)
		receivedItems.fuses[itemID - 19].amount++;
	else if (itemID < 111)
		receivedItems.abilities[itemID - 100].amount++;

	if (notifyPlayer)
	{
		pendingItemIDs.Emplace(itemID);
	}
}

/**
 * @brief Retains only digits ('0'?'9') and the dot ('.') character from an version string
 * @note Intended to extract version-like strings
 * @param version The string to filter
 * @return The filtered string
 */
static std::string filterVersion(std::string version)
{
	std::string result;
	result.reserve(version.size());

	for (unsigned char c : version)
	{
		if (std::isdigit(c) || c == '.')
			result.push_back(c);
	}

	return result;
}

/**
*   @brief Get the version of the apworld from the slot_data
*   @param version: The version
*/
static void WorldVersionCallback(std::string version)
{
	latestWorldVersion = filterVersion(version);
}

/**
*   @brief Log used to check APCpp messages, replace prints in APCpp code by call of this function
*   @param message: The message to log
*/
void LogFromAPCpp(std::string message) {
	Output::send<LogLevel::Verbose>(TEXT("LogFromAPCpp: {}\n"), RC::to_wstring(message).c_str());
}

namespace ModConsole {
	/**
	*   @brief Reset all received items amounts and all unlocked train components to 0
	*/
	void ModConsole::ResetItemAmounts()
	{
		for (int32_t index = 0; index != receivedItems.champs.Num(); index++)
		{
			receivedItems.champs[index].amount = 0;
		}

		for (int32_t index = 0; index != receivedItems.fuses.Num(); index++)
		{
			receivedItems.fuses[index].amount = 0;
		}

		for (int32_t index = 0; index != receivedItems.abilities.Num(); index++)
		{
			receivedItems.abilities[index].amount = 0;
		}

		pendingItemIDs.Empty();
	}

	/**
	*   @brief Manage user commands written in the UE console to interact with Archipelago using APCpp
	*   @param Ar: The used device (expecting UE console here)
	*   @param command: The typed command by the user
	*/
	bool ModConsole::CheckCommand(FOutputDevice& Ar, const TCHAR* command)
	{
		// Conversion from const TCHAR* to const char*
		// First convert to string
		string commandStr = to_string(command);
		const char* commandCharStr = commandStr.c_str();

		// If no '/' and '!' starts the entry, exit early, not a command
		if (commandCharStr[0] != '/' && commandCharStr[0] != '!')
		{
			Output::send<LogLevel::Verbose>(STR("Entry with no command found: {}\n"), command);
			Output::send<LogLevel::Verbose>(STR("Put \'/\' or \'!\' at the start of your command to detect it\n"));
			Output::send<LogLevel::Verbose>(STR("Try /help for details\n"));

			return false;
		}
		commandCharStr++;

		char outOptions[ALL_OPTIONS_MAX_LENGTH];
		int outOptionPositions[NB_MAX_OPTIONS];
		int numberOfOptions = 0;

		numberOfOptions = CompareAndParseCmd(commandCharStr, "help", outOptions, outOptionPositions);
		if (numberOfOptions != UNDEFINED_COMMAND)
		{
			// Print to the console
			Ar.Log(STR("/help\n"));
			Ar.Log(STR("        Shows all available commands and their description\n"));
			Ar.Log(STR("/connect <IP> <PlayerName> [Password]\n"));
			Ar.Log(STR("        Connect to the AP room using an <IP>, a <PlayerName> (in the forms \"archipelago.gg:XXXXX\" and \"2XKO\") and an optional [Password]\n"));
			Ar.Log(STR("/disconnect\n"));
			Ar.Log(STR("        Disconnect from the current room\n"));

			return true;
		}

		// Check for the /connect command
		numberOfOptions = CompareAndParseCmd(commandCharStr, "connect", outOptions, outOptionPositions);
		if (numberOfOptions >= 2)
		{
			Ar.Log(STR("Connect\n"));
			const char* ipAddress = GetOptionAtindex(outOptions, outOptionPositions, 0);
			const char* playerName = GetOptionAtindex(outOptions, outOptionPositions, 1);
			const char* password = numberOfOptions == 2 ? "" : GetOptionAtindex(outOptions, outOptionPositions, 2);

			AP_Init(ipAddress, "2XKO - The Climb", playerName, password);
			AP_SetItemClearCallback(ClearInventoryCallback);
			AP_SetItemRecvCallback(ItemReceivedCallback);
			AP_SetLocationCheckedCallback(LocationCheckedCallback);
			AP_RegisterSlotDataRawCallback("world_version", &WorldVersionCallback);
			AP_SetDeathLinkSupported(false);
			AP_Start();

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "disconnect", outOptions, outOptionPositions);
		if (numberOfOptions >= 0)
		{
			Ar.Log(STR("Disconnect\n"));
			ModConsole::ResetItemAmounts();
			AP_Shutdown();

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "release", outOptions, outOptionPositions);
		if (numberOfOptions >= 0)
		{
			/* code */
			Ar.Log(STR("Release\n"));

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "collect", outOptions, outOptionPositions);
		if (numberOfOptions >= 0)
		{
			/* code */
			Ar.Log(STR("Collect\n"));

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "hint", outOptions, outOptionPositions);
		if (numberOfOptions >= 1)
		{
			/* code */
			Ar.Log(STR("Hint\n"));

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "hint_location", outOptions, outOptionPositions);
		if (numberOfOptions >= 1)
		{
			/* code */
			Ar.Log(STR("Hint location\n"));

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "remaining", outOptions, outOptionPositions);
		if (numberOfOptions >= 0)
		{
			/* code */
			Ar.Log(STR("Remaining\n"));

			return true;
		}

		numberOfOptions = CompareAndParseCmd(commandCharStr, "send", outOptions, outOptionPositions);
		if (numberOfOptions >= 1) // send a location as done check
		{
			/* code */
			Ar.Log(STR("Send\n"));

			return true;
		}
		
		return false;
	}
}