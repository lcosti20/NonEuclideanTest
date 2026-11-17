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
 *
 * Objects (any owner that isn't a pawn, e.g. a physics prop): the tracked point is the root
 * component's origin, so give props a centred pivot. On a crossing the whole actor transform is
 * warped, so a scaling portal resizes the object and a mirroring portal gives it a negative scale:
 * its shape really is mirrored in the world (IsMirrored). A simulating root keeps its motion: linear
 * and angular velocity are warped, and its mass is recomputed after a size change. If the object is
 * tagged with a space, it moves to the exit portal's space. While the object is partway through a
 * portal, a copy of its static meshes is shown at the other side (bShowCopyWhileCrossing), so it
 * doesn't vanish into the portal surface, and both are cut at the portal plane (bClipAtPortal), so
 * each part is drawn on one side only. Simple collision made of boxes, spheres or convex hulls
 * on a single body works mirrored; welded multi-component bodies don't rescale correctly.
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

	/**
	 * Objects only (owners that aren't pawns): while the owner is partway through a portal, show a
	 * copy of its static meshes at the linked portal, so the part that's already through is visible.
	 * The copy is solid to pawns (it uses the meshes' simple collision), and a character walking into
	 * it pushes the real object, using the character movement's push settings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bShowCopyWhileCrossing = true;

	/**
	 * Objects only: while the owner is partway through a portal, cut it (and its copy) at the portal
	 * plane, so each part appears on one side only. The owner's materials must call the
	 * /PortalWorlds/Materials/MF_PortalClip material function (Masked blend mode, Opacity Mask);
	 * other materials ignore the clip plane. The plane is written to custom primitive data
	 * ClipPlaneDataIndex..+3 of every primitive.
	 * While cut, the owner also stops colliding with pawns (ECC_Pawn) that stand on the cut-away side
	 * near the portal, so they can't bump into the invisible part before they walk through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bClipAtPortal = true;

	/** First of the four custom primitive data floats holding the clip plane (read by MF_PortalClip). */
	static constexpr int32 ClipPlaneDataIndex = 32;

	/** True while the owner is mirrored relative to the world (after an odd number of mirroring portals). */
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsMirrored() const { return bMirrored; }

	/** Sets the mirrored state directly, e.g. on respawn. */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetMirrored(bool bNewMirrored) { bMirrored = bNewMirrored; }

	/**
	 * Rebuilds the render state of Actor's primitives. Call after giving an actor a scale of the
	 * opposite sign (mirroring or unmirroring it): a mesh decides which faces to cull when its render
	 * state is created, and keeps that choice when only its transform changes, so it would render
	 * inside out. Traversal does this itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	static void RefreshMirroredRendering(AActor* Actor);

	/** The traversal component of a player's view target (or pawn), if it has one. */
	static UPortalTraversalComponent* FindForPlayer(const APlayerController* PC);

	/**
	 * Objects: restores the owner's pawn collision if it was turned off because a pawn stood on the
	 * cut-away side of a portal. Call before changing the owner's ECC_Pawn response yourself (the grab
	 * component does), so the response you read and later restore is the real one.
	 */
	void StopIgnoringPawns();

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
	void TraverseObject(APortal* From, APortal* To, const FMatrix& Warp, const FVector& Bump);
	void UpdateCrossingCopy();
	static void SetClipPlane(AActor* Actor, const FVector4f& Plane, FVector4f& InOutCurrent);
	void WarnIfMaterialsIgnoreClip() const;
	void UpdatePawnCollision(const APortal* Through, const FVector& KeepNormal);
	bool IsAnyPawnOverlapping() const;
	bool IsPawnOnCutSide(const AActor* Actor, const APortal* Portal, const FVector& KeepNormal) const;
	ECollisionResponse GetOwnPawnResponse() const;
	void SetCopyPawnResponse(ECollisionResponse Response);
	UFUNCTION()
	void OnCopyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	AActor* GetOrCreateCrossingCopy();
	void MirrorPendingInput();
	void UpdateInputTickPrerequisites();

	FPortalInputMirrorTickFunction InputMirrorTick;
	TWeakObjectPtr<AController> InputTickController;
	bool bMirrored = false;

	/** Objects: copy of the owner's static meshes shown at the far side while it's partway through a portal. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> CrossingCopy;

	/** Whether the copy was mirrored when its render state was last built. */
	bool bCrossingCopyMirrored = false;

	/** Warp from the owner to the copy, as last placed (pushes on the copy go back through its inverse). */
	FMatrix CopyWarp = FMatrix::Identity;

	/** The copy's current ECC_Pawn response. */
	TEnumAsByte<ECollisionResponse> CopyPawnResponse = ECR_Ignore;

	/** Clip planes last written to the owner's and the copy's primitives (zero: no clipping). */
	FVector4f OwnerClipPlane = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f CopyClipPlane = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);

	/** Objects: true while the owner's ECC_Pawn response was set to Ignore by UpdatePawnCollision. */
	bool bIgnoringPawns = false;
	TEnumAsByte<ECollisionResponse> PawnResponseBeforeIgnore = ECR_Block;

	/** Whether the owner's materials were checked for MF_PortalClip (done once, on the first cut). */
	bool bCheckedClipMaterials = false;

	FVector PreviousEye = FVector::ZeroVector;
	bool bHasPreviousEye = false;
	int32 TraversalCount = 0;
};
