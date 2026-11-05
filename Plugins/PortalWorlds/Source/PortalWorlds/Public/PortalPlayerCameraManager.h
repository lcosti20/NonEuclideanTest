#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "PortalPlayerCameraManager.generated.h"

/**
 * Player camera manager that renders the portals once the camera has its final position for the
 * frame, so portal views never lag the player's view. While the view target is mirrored (see
 * UPortalTraversalComponent::IsMirrored) it also reverses yaw input, since the view is shown
 * flipped left-right.
 *
 * Use it (or a subclass) as your player controller's PlayerCameraManagerClass. If you already have
 * your own camera manager, add this instead, after Super::UpdateCamera in its UpdateCamera:
 *     if (UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>())
 *     {
 *         Portals->RenderPortals(PCOwner, GetCameraCacheView());
 *     }
 * and, for mirroring portals, at the start of its ProcessViewRotation:
 *     UPortalSubsystem::MirrorViewRotationInput(PCOwner, OutDeltaRot);
 */
UCLASS()
class PORTALWORLDS_API APortalPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	virtual void UpdateCamera(float DeltaTime) override;
	virtual void ProcessViewRotation(float DeltaTime, FRotator& OutViewRotation, FRotator& OutDeltaRot) override;
};
