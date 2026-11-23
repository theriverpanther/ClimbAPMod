#pragma once

using namespace RC;
using namespace RC::Unreal;

extern UObject* ItemManager;
extern UFunction* ItemReceievedEvent;

typedef struct
{
	FString name;
	int32_t amount;
}RandoItem;

typedef struct
{
	TArray<RandoItem> champs;
	TArray<RandoItem> fuses;
	TArray<RandoItem> abilities;
}ReceivedItems;

extern std::string latestWorldVersion;
extern ReceievedItems receivedItems;
extern TArray<int64_t> pendingItemIDs;

void LogFromAPCpp(std::string message);

namespace ModConsole {
	using RC::Unreal::FOutputDevice;
	using RC::Unreal::TCHAR;

	bool CheckCommand(FOutputDevice& Ar, const TCHAR* command);
	void ResetItemAmounts();
}