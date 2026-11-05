#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PortalTraversalComponent.generated.h"

class APortal;
class AController;
class APlayerController;
class UPortalTraversalComponent;

/** Runs between the controller's input processing and the pawn's movement, to mirror movement input. */
struct FPortalInputMirrorTickFunction : public FTickFunction
{
	UPortalTraversalComponent* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("PortalTraversal input mirror"); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPortalTraversedSignature, APortal*, FromPortal, APortal*, ToPortal, float, ScaleFactor);

/**
 * Teleports its owner through portals. Add it to the player pawn (or anything that should pass
 * through portals); it uses the owner's camera automatically.
 *
 * After movement each frame, the path of the Eye component (the camera) since the last frame is
 * tested against every portal rectangle, with the plane bumped 2 mm x scale towards the side the
 * eye came from. On a crossing, the owner's location, control rotation and velocity are warped
 * to the linked portal, shifted twice the bump past the exit plane, and the camera is flagged as
 * cut. Because the eye never reaches the surface before teleporting, the near plane never
 * clips through it. A scaled warp (portals of different sizes) also resizes the owner.
 *
 * Teleporting happens after movement and before APortalPlayerCameraManager::UpdateCamera, so
 * the frame is rendered from the new position.
 *
 * Mirroring portals flip the owner's handedness relative to the world (IsMirrored), and the next
 * one flips it back. A camera can't render mirrored, so while mirrored the player's view is
 * flipped left-right on screen (UPortalSubsystem adds a post-process flip), mouse yaw is reversed
 * (APortalPlayerCameraManager) and movement input is reflected left-right (bMirrorMovementInput),
 * so the controls feel unchanged.
 */
UCLASS(ClassGroup = (Portals), meta = (BlueprintSpawnableComponent))
class PORTALWORLDS_API UPortalTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPortalTraversalComponent();

	/**
	 * The point that must cross a portal: normally the camera. Leave empty to use the owner's
	 * camera component, or its root if it has none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	TObjectPtr<USceneComponent> Eye;

	/** Called after each trip through a portal. ScaleFactor is the size change (1 for a plain portal). */
	UPROPERTY(BlueprintAssignable, Category = "Portal")
	FOnPortalTraversedSignature OnPortalTraversed;

	/**
	 * While mirrored, reflect the owner's movement input left-right before its movement component
	 * uses it, so "strafe right" still moves to the right on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bMirrorMovementInput = true;

	/** True while the owner is mirrored relative to the world (after an odd number of mirroring portals). */
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsMirrored() const { return bMirrored; }

	/** Sets the mirrored state directly, e.g. on respawn. */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetMirrored(bool bNewMirrored) { bMirrored = bNewMirrored; }

	/** The traversal component of a player's view target (or pawn), if it has one. */
	static UPortalTraversalComponent* FindForPlayer(const APlayerController* PC);

	/** Number of portals passed through since play began. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	int32 GetTraversalCount() const { return TraversalCount; }

	/**
	 * Forget the eye's previous position. Call after moving the owner discontinuously by other
	 * means (respawn, scripted teleport), or the jump itself may count as crossing a portal.
	 */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void ResetTracking() { bHasPreviousEye = false; }

	/**
	 * Resizes the owner by Factor: actor scale (capsule and camera height) and, for characters,
	 * every movement distance/speed/acceleration, so it moves the same relative to its own size.
	 * Applied automatically by portals of different sizes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void ApplyScaleFactor(double Factor);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	friend struct FPortalInputMirrorTickFunction;

	void Traverse(APortal* From, APortal* To, const FVector& Bump);
	void MirrorPendingInput();
	void UpdateInputTickPrerequisites();

	FPortalInputMirrorTickFunction InputMirrorTick;
	TWeakObjectPtr<AController> InputTickController;
	bool bMirrored = false;

	FVector PreviousEye = FVector::ZeroVector;
	bool bHasPreviousEye = false;
	int32 TraversalCount = 0;
};
