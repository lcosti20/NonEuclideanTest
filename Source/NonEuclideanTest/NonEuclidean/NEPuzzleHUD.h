#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NEPuzzleHUD.generated.h"

/**
 * HUD for the object puzzle map: a crosshair, the controls, the player's size and handedness,
 * what's held or aimed at (with its size and handedness), and a checklist of the sockets.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEPuzzleHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
