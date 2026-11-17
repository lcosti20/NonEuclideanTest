#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NEStartMenuPlayerController.generated.h"

class UNEMenuWidget;

/**
 * Controller for the start menu map. Lists every level in Content/NonEuclidean/Maps (found through
 * the asset registry, so a new map appears without code changes) and opens the one chosen.
 * Up/Down and Enter, or the number keys 1-9.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEStartMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

protected:
	virtual void SetupInputComponent() override;

private:
	void Refresh();
	void MoveSelection(int32 Delta);
	void Choose(int32 Index);

	/** Package paths of the levels, sorted by name. */
	TArray<FString> Levels;
	int32 Selected = 0;

	UPROPERTY()
	TObjectPtr<UNEMenuWidget> MenuWidget;
};
