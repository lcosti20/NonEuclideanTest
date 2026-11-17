#include "NEStartMenuGameMode.h"

#include "NEStartMenuPlayerController.h"

ANEStartMenuGameMode::ANEStartMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ANEStartMenuPlayerController::StaticClass();

	for (const TCHAR* Name : {
		TEXT("L_NE_House3"), TEXT("L_NE_House6"), TEXT("L_NE_Mirror"), TEXT("L_NE_ObjectPuzzles"), TEXT("L_NE_Pillar"),
		TEXT("L_NE_PortalSpike"), TEXT("L_NE_Scale"), TEXT("L_NE_Slope"), TEXT("L_NE_Tunnels") })
	{
		const FString Path = FString::Printf(TEXT("/Game/NonEuclidean/Maps/%s.%s"), Name, Name);
		Levels.Add(TSoftObjectPtr<UWorld>(FSoftObjectPath(Path)));
	}
}
