#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "NEStartMenuGameMode.generated.h"

/**
 * Game mode for the start menu map (L_NE_StartMenu): no pawn, just ANEStartMenuPlayerController.
 * The game starts here, and Esc > yes in a level opens it again.
 *
 * Levels are listed from soft references, which packaging follows, so every level is in the build.
 * Add a new map here to list it.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEStartMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANEStartMenuGameMode();

	/** The start menu map. */
	static const TCHAR* MapPath() { return TEXT("/Game/NonEuclidean/Maps/L_NE_StartMenu"); }

	/** The levels the menu lists. */
	UPROPERTY(EditAnywhere, Category = "Start Menu")
	TArray<TSoftObjectPtr<UWorld>> Levels;
};
