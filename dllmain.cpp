#include <stdio.h>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>

#include "Archipelago.h"

const std::string CURRENT_WORLD_VERSION = "1.0.0"; // To update when the version of the AP logic changes
std::string latestWorldVersion;

UObject* ItemManager = NULL;

UFunction* ItemReceivedEvent = NULL;
UFunction* ArchipelagoMessageEvent = NULL;
UFunction* GetAllItemAmountsEvent = NULL;
UFunction* ConnectionStatusUpdatedEvent = NULL;
UFunction* CheckItemUnlockedEvent = NULL;
UFunction* UpdateUnlockedChampsEvent = NULL;
UFunction* UpdateUnlockedMovesetEvent = NULL;

static bool authenticated = false;
static bool isNewGame = false;

class ClimbAPMod : public RC::CppUserModBase
{
public:
    ClimbAPMod() : CppUserModBase()
    {
        ModName = STR("ClimbAPMod");
        ModVersion = STR(0.1);
        ModDescription = STR("Adds an Archipelago Client integration to The Climb");
        ModAuthors = STR("RiverPanther");

        Output::send<LogLevel::Verbose>(STR("Mod name: {}\n"), ModName);
        Output::send<LogLevel::Verbose>(STR("Version: {}\n"), ModVersion);
        Output::send<LogLevel::Verbose>(STR("{}\n"), ModDescription);
        Output::send<LogLevel::Verbose>(STR("Author: {}\n"), ModAuthors);
    }

    ~ClimbAPMod() override
    {
    }

    static bool APAuthCheck()
    {
        if (AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated)
        {
            Output::send<LogLevel::Verbose>(STR("The player is not authenticated\n"));
            return true;
        }
        return false;
    }

    static auto CallbackFunctionHook([[maybe_unused]] Unreal::UObject* Context, Unreal::FFrame& Stack, [[maybe_unused]] void* RESULT_DECL) -> void
    {
        // Set hooked functions and events once
        static auto SendLocationIDHook = FName(STR("SendLocationID"), FNAME_Add); // Send locationID to Archipelago
        static auto NewGameHook = FName(STR("NewGame"), FNAME_Add); // Function called at the start of each run
        static auto ReloadedGameHook = FName(STR("GameReloaded"), FNAME_Add); // Function called whenever the game is reloaded
        static auto ClimbLossHook = FName(STR("ClimbLoss"), FNAME_Add); // Function called when the player dies with both champions
        static auto CheckPendingMessageHook = FName(STR("CheckPendingMessage"), FNAME_Add); // Show the last Archipelago pending message
        static auto GetConnectionStatusHook = FName(STR("GetConnectionStatus"), FNAME_Add); // Check if the player is authenticated
        static auto GetPendingItemsHook = FName(STR("GetPendingItems"), FNAME_Add); // Check if the player has received new items
        static auto IsChampUnlockedHook = FName(STR("IsChampUnlocked"), FNAME_Add); // Checks if champions are unlocked by indexing
        static auto IsFuseUnlockedHook = FName(STR("IsFuseUnlocked"), FNAME_Add); // Checks if fuses are unlocked by indexing
        static auto IsAbilityUnlockedHook = FName(STR("IsAbilityUnlocked"), FNAME_Add); // Checks if abilities are unlocked by indexing

        // Check if the events are named properly
        if (Stack.Node()->GetNamePrivate() == SendLocationIDHook)
        {
            Output::send<LogLevel::Verbose>(STR("SendLocationIDHook\n"));

            // Abort if connection unstable
            if (APAuthCheck()) return;

            int64_t* locationID = Stack.Node()->GetPropertyByName(STR("locationID"))->ContainerPtrToValuePtr<int64_t>(Stack.Locals());
            Output::send<LogLevel::Verbose>(STR("{}\n"), *locationID);

            // If the ID is -1, no item sent
            if (*locationID == -1)
            {
                Output::send<LogLevel::Error>(STR("Item not found: locationID == -1\n"));
                return;
            }

            AP_SendItem(*locationID);
        }

        else if (Stack.Node()->GetNamePrivate() == NewGameHook)
        {

        }

        else if (Stack.Node()->GetNamePrivate() == ReloadedGameHook)
        {
            ItemManager = NULL;
            ItemReceivedEvent = NULL;
            ArchipelagoMessageEvent = NULL;
            GetAllItemAmountsEvent = NULL;
            ConnectionStatusUpdatedEvent = NULL;
            CheckItemUnlockedEvent = NULL;
            UpdateUnlockedChampsEvent = NULL;
            UpdateUnlockedMovesetEvent = NULL;
            authenticated = false;
        }
        
        else if (Stack.Node()->GetNamePrivate() == ClimbLossHook)
        {
            Output::send<LogLevel::Verbose>(STR("ClimbLossHook\n"));

            // Abort if connection unstable
            if (APAuthCheck()) return;

            AP_StoryComplete();
        }

        else if (Stack.Node()->GetNamePrivate() == CheckPendingMessageHook)
        {
            if (AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated && AP_IsMessagePending())
            {
                if (ItemManager != NULL)
                {
                    if (ArchipelagoMessageEvent == NULL)
                    {
                        Output::send<LogLevel::Error>(STR("ArchipelagoMessage not found\n"));
                        return;
                    }

                    FString message = FString(to_wstring(AP_GetLatestMessage()->text).c_str());
                    Output::send<LogLevel::Verbose>(STR("Pending message: {}\n"), to_wstring(AP_GetLatestMessage()->text).c_str());
                    if (ItemManager != NULL)
                    {
                        ItemManager->ProcessEvent(ArchipelagoMessageEvent, &message);
                    }
                    AP_ClearLatestMessage();
                }
            }
        }

        else if (Stack.Node()->GetNamePrivate() == GetConnectionStatusHook)
        {
            if (ItemManager != NULL)
            {
                if (ConnectionStatusUpdatedEvent == NULL)
                {
                    Output::send<LogLevel::Error>(STR("connectionStatusUpdated not found\n"));
                    return;
                }

                if (!authenticated && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated
                    || authenticated && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated)
                {
                    ConnectionMessage connectionMessage;

                    authenticated = !authenticated;
                    connectionMessage.autheticated = authenticated;

                    if (!authenticated)
                    {
                        conectionMessage.statusMessage = FString(RC::to_wstring("Offline").c_str());
                    }

                    else if (CURRENT_WORLD_VERSION == latestWorldVersion)
                    {
                        connectionMessage.statusMessage = FString(RC::to_wstring("Connected").c_str());
                    }
                    else
                    {
                        connectionMessage.statusMessage = FString(RC::to_wstring(
                            "Mod update required\nv" + CURRENT_WORLD_VERSION + " -> v" + latestWorldVersion
                        ).c_str());
                    }
                    ItemManager->ProcessEvent(ConnectionStatusUpdatedEvent, &connetionMessage);
                }
            }
        }

        else if (Stack.Node()->GetNamePrivate() == GetPendingItemsHook)
        {
            if (ItemReceivedEvent == NULL)
            {
                Output::send<LogLevel::Error>(STR("ItemReceivedEvent not found\n"));
                return;
            }

            if (pendingItemIDs.Num() > 0)
            {
                ItemManager->ProcessEvent(ItemReceivedEvent, &pendingItemIDs);
                pendingItemIDs.Empty();
            }
        }

        else if (Stack.Node()->GetNamePrivate() == IsChampUnlockedHook)
        {
            Output::send<LogLevel::Verbose>(STR("IsChampUnlockedHook\n"));

            int32_t* index = Stack.Node()->GetPropertyByName(STR("ChampIndex"))->ContainerPtrToValuePtr<int32_t>(Stack.Locals));
            ItemManager->ProcessEvent(CheckItemUnlockedEvent, &receivedItems.champions[*index].unlocked);
        }

        else if (Stack.Node()->GetNamePrivate() == IsFuseUnlockedHook)
        {
            Output::send<LogLevel::Verbose>(STR("IsFuseUnlockedHook\n"));

            int32_t* index = Stack.Node()->GetPropertyByName(STR("FuseIndex"))->ContainerPtrToValuePtr<int32_t>(Stack.Locals));
            ItemManager->ProcessEvent(CheckItemUnlockedEvent, &receivedItems.fuses[*index].unlocked);
        }

        else if (Stack.Node()->GetNamePrivate() == IsAbilityUnlockedHook)
        {
            Output::send<LogLevel::Verbose>(STR("IsAbilityUnlockedHook\n"));

            int32_t* index = Stack.Node()->GetPropertyByName(STR("AbilityIndex"))->ContainerPtrToValuePtr<int32_t>(Stack.Locals));
            ItemManager->ProcessEvent(CheckItemUnlockedEvent, &receivedItems.abilities[*index].unlocked);
        }
    }

    static bool CallbackConsole(UObject* object, const Unreal::TCHAR* command, FOutputDevice& Ar, UObject* executor)
    {
        bool same = ModConsole::CheckCommand(Ar, command);

        if (same)
        {
            Output::send<LogLevel::Verbose>(STR("Command detected with success: {}\n"), command);
        }
        else
        {
            Output::send<LogLevel::Verbose>(STR("Command not found or missing arguments: {}\n"), command);
        }

        return same;
    }

    auto on_unreal_init() -> void override
    {
        auto Object = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/CoreUObject.Object"));
        Output::send<LogLevel::Verbose>(STR("Object Name: {}\n"), Object->GetFullName());

        Hook::RegisterProcessConsoleExecCallback(CallbackConsole);
        receivedItems.champs.SetNum(18);
        receivedItems.champs[0].name = FString(to_wstring("Ahri").c_str());
        receivedItems.champs[1].name = FString(to_wstring("Akali").c_str());
        receivedItems.champs[2].name = FString(to_wstring("Blitzcrank").c_str());
        receivedItems.champs[3].name = FString(to_wstring("Braum").c_str());
        receivedItems.champs[4].name = FString(to_wstring("Caitlyn").c_str());
        receivedItems.champs[5].name = FString(to_wstring("Darius").c_str());
        receivedItems.champs[6].name = FString(to_wstring("Ekko").c_str());
        receivedItems.champs[7].name = FString(to_wstring("Illaoi").c_str());
        receivedItems.champs[8].name = FString(to_wstring("Jinx").c_str());
        receivedItems.champs[9].name = FString(to_wstring("Lux").c_str());
        receivedItems.champs[10].name = FString(to_wstring("Samira").c_str());
        receivedItems.champs[11].name = FString(to_wstring("Senna").c_str());
        receivedItems.champs[12].name = FString(to_wstring("Sett").c_str());
        receivedItems.champs[13].name = FString(to_wstring("Teemo").c_str());
        receivedItems.champs[14].name = FString(to_wstring("Thresh").c_str());
        receivedItems.champs[15].name = FString(to_wstring("Vi").c_str());
        receivedItems.champs[16].name = FString(to_wstring("Warwick").c_str());
        receivedItems.champs[17].name = FString(to_wstring("Yasuo").c_str());

        for (int i = 0; i < receivedItems.champs.Num(); i++)
        {
            receivedItems.champs[i].amount = 0;
        }

        receivedItems.fuses.SetNum(6);
        receivedItems.fuses[0].name = FString(to_wstring("Double Down").c_str());
        receivedItems.fuses[1].name = FString(to_wstring("Juggernaut").c_str());
        receivedItems.fuses[2].name = FString(to_wstring("Sidekick").c_str());
        receivedItems.fuses[3].name = FString(to_wstring("2X Assist").c_str());
        receivedItems.fuses[4].name = FString(to_wstring("Freestyle").c_str());
        receivedItems.fuses[5].name = FString(to_wstring("Teamfight").c_str());

        for (int i = 0; i < receivedItems.fuses.Num(); i++)
        {
            receivedItems.fuses[i].amount = 0;
        }

        receivedItems.abilities.SetNum(11);
        receivedItems.abilities[0].name = FString(to_wstring("Progressive S1").c_str());
        receivedItems.abilities[1].name = FString(to_wstring("Progressive S2").c_str());
        receivedItems.abilities[2].name = FString(to_wstring("Jump").c_str());
        receivedItems.abilities[3].name = FString(to_wstring("Crouch").c_str());
        receivedItems.abilities[4].name = FString(to_wstring("Block").c_str());
        receivedItems.abilities[5].name = FString(to_wstring("Ultimate").c_str());
        receivedItems.abilities[6].name = FString(to_wstring("Grab").c_str());
        receivedItems.abilities[7].name = FString(to_wstring("Parry").c_str());
        receivedItems.abilities[8].name = FString(to_wstring("Assist").c_str());
        receivedItems.abilities[9].name = FString(to_wstring("Break").c_str());
        receivedItems.abilities[10].name = FString(to_wstring("Taunt").c_str());

        for (int i = 0; i < receivedItems.abilities.Num(); i++)
        {
            receivedItems.abilities[i].amount = 0;
        }

        // Callback setup
        if (UObject::ProcessLocalScriptFunctionInternal.is_ready() && Unreal::Version::IsAtLeast(4, 22))
        {
            Output::send(STR("Enabling custom hook events\n"));
            Hook::RegisterProcessLocalScriptFunctionPostCallback(CallbackFunctionHook);
        }
    }

    auto on_update() -> void override
    {
        // On load, establish item manager connection
        if (ItemManager == NULL)
        {
            ItemManager = UObjectGlobals::FindFirstOf(STR("ItemManager_C"));
        }

        // Actions that depend on the item manager
        if (ItemManager != NULL)
        {
            // Set events if they are empty
            if (ItemReceivedEvent == NULL)
            {
                static auto ItemReceived = FName(STR("ItemReceived"), FNAME_Add);
                ItemReceivedEvent = ItemManager->GetFunctionByName(ItemReceived);
            }

            if (ArchipelagoMessageEvent == NULL)
            {
                static auto ArchipelagoMessage = FName(STR("ArchipelagoMessage"), FNAME_Add);
                ArchipelagoMessageEvent = ItemManager->GetFunctionByName(ArchipelagoMessage);
            }

            if (CheckItemUnlockedEvent == NULL)
            {
                static auto CheckItemUnlocked = FName(STR("CheckItemUnlocked"), FNAME_Add);
                CheckItemUnlockedEvent = ItemManager->GetFunctionByName(CheckItemUnlocked);
            }

            if (ConnectionStatusUpdatedEvent == NULL)
            {
                static auto ConnectionStatusUpdated = FName(STR("ConnectionStatusUpdated"), FNAME_Add);
                ConnectionStatusUpdatedEvent = ItemManager->GetFunctionByName(ConnectionStatusUpdated);
            }

            if (UpdateUnlockedChampsEvent == NULL)
            {
                static auto UpdateUnlockedChamps = FName(STR("UpdateUnlockedChamps"), FNAME_Add);
                CheckItemUnlockedEvent = ItemManager->GetFunctionByName(UpdateUnlockedChamps);
            }

            if (UpdateUnlockedMovesetEvent == NULL)
            {
                static auto UpdateUnlockedMoveset = FName(STR("UpdateUnlockedMoveset"), FNAME_Add);
                CheckItemUnlockedEvent = ItemManager->GetFunctionByName(UpdateUnlockedMoveset);
            }

            if (isNewGame && AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated)
            {
                if (GetAllItemAmountsEvent == NULL)
                {
                    Output::send<LogLevel::Error>(STR("GetAllItemAmountsEvent not found\n"));
                }
                else
                {
                    pendingItemIDs.Empty();
                    ItemManager->ProcessEvent(GetAllItemAmountsEvent, &receivedItems);
                    isNewGame = false;
                }
            }
        }
    }
};

#define CLIMB_AP_MOD_API __declspec(dllexport)
extern "C"
{
    CLIMB_AP_MOD_API RC::CppUserModBase* start_mod()
    {
        return new ClimbAPMod();
    }

    CLIMB_AP_MOD_API void uninstall_mod(RC::CppUserModBase mod)
    {
        delete mod;
    }
}