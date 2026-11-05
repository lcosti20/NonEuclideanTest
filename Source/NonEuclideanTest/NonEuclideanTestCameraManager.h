// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PortalPlayerCameraManager.h"
#include "NonEuclideanTestCameraManager.generated.h"

/**
 *  Basic First Person camera manager.
 *  Limits min/max look pitch. Portal rendering comes from APortalPlayerCameraManager (PortalWorlds).
 */
UCLASS()
class ANonEuclideanTestCameraManager : public APortalPlayerCameraManager
{
	GENERATED_BODY()

public:

	/** Constructor */
	ANonEuclideanTestCameraManager();
};
