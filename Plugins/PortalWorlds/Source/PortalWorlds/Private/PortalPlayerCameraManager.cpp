#include "PortalPlayerCameraManager.h"

#include "Engine/World.h"
#include "PortalSubsystem.h"

void APortalPlayerCameraManager::UpdateCamera(float DeltaTime)
{
	Super::UpdateCamera(DeltaTime);

	if (UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>())
	{
		Portals->RenderPortals(PCOwner, GetCameraCacheView());
	}
}

void APortalPlayerCameraManager::ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot)
{
	UPortalSubsystem::MirrorViewRotationInput(PCOwner, OutDeltaRot);
	Super::ProcessViewRotation(DeltaTime, OutViewRotation, OutDeltaRot);
}
