#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PortalGrabComponent.generated.h"

class APortal;
class UPhysicsHandleComponent;
class UPrimitiveComponent;
class UPortalTraversalComponent;

/**
 * Lets a player pick up, carry and throw physics objects, including through portals. Add it to the
 * player pawn next to its Portal Traversal component, and bind input to ToggleGrab and Throw.
 *
 * The held object is pulled by a physics handle towards a point in front of the eye, keeping its
 * orientation relative to the player's heading. Everything is worked out in the player's own frame
 * (position, view yaw, size and mirrored state), so carrying an object through a portal warps it
 * along with the player:
 *  - The object usually reaches a portal first. When its centre crosses, its own Portal Traversal
 *    component warps it (resized and/or mirrored), and the hold point follows it through.
 *  - When the player crosses after it, the two warps cancel, and the object stays put in front of them.
 * So an object carried through a shrinking portal comes out shrunk with the player, and one carried
 * through a mirroring portal comes out mirrored. To change an object relative to the player, throw
 * it through, or carry it through and walk back around.
 *
 * Aiming also looks through portals: an object seen through a portal can be picked up.
 *
 * Objects must simulate physics. Give them a Portal Traversal component and a centred pivot, or
 * they can't pass through portals. While held, an object ignores collision with pawns.
 */
UCLASS(ClassGroup = (Portals), meta = (BlueprintSpawnableComponent))
class PORTALWORLDS_API UPortalGrabComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPortalGrabComponent();

	/** How far away an object can be picked up (cm, at the player's normal size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal Grab")
	float GrabRange = 250.0f;

	/** Gap between the eye and the held object's bounding sphere (cm, at the player's normal size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal Grab")
	float HoldGap = 50.0f;

	/** Largest bounding-sphere radius that can be picked up (cm, relative to the player's size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal Grab")
	float MaxObjectRadius = 90.0f;

	/** Throw speed (cm/s, at the player's normal size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal Grab")
	float ThrowSpeed = 700.0f;

	/**
	 * The object is dropped if it stays this far from where it should be for half a second, e.g. stuck
	 * behind a wall (cm, at the player's normal size).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal Grab")
	float BreakDistance = 120.0f;

	/** Picks up the object under the crosshair. Returns true if something was picked up. */
	UFUNCTION(BlueprintCallable, Category = "Portal Grab")
	bool Grab();

	/** Drops the held object. */
	UFUNCTION(BlueprintCallable, Category = "Portal Grab")
	void Release();

	/** Throws the held object forwards. */
	UFUNCTION(BlueprintCallable, Category = "Portal Grab")
	void Throw();

	/** Drops the held object, or picks one up. */
	UFUNCTION(BlueprintCallable, Category = "Portal Grab")
	void ToggleGrab();

	UFUNCTION(BlueprintPure, Category = "Portal Grab")
	bool IsHolding() const { return HeldComponent.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Portal Grab")
	UPrimitiveComponent* GetHeldComponent() const { return HeldComponent.Get(); }

	/** The object that Grab would pick up now, or null. */
	UFUNCTION(BlueprintPure, Category = "Portal Grab")
	UPrimitiveComponent* GetAimedComponent() const;

	/** The held object's size relative to the player (1 when they've been through the same portals). */
	UFUNCTION(BlueprintPure, Category = "Portal Grab")
	float GetHeldRelativeScale() const;

	/** Where the held object is being pulled to (world space, on the object's side of any portal). */
	FTransform GetHoldTarget() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void OnOwnerTraversed(APortal* FromPortal, APortal* ToPortal, float ScaleFactor);

	UFUNCTION()
	void OnHeldTraversed(APortal* FromPortal, APortal* ToPortal, float ScaleFactor);

	/** Traces along the view, through up to two portals. OutWarp maps the player's side to the hit's side. */
	UPrimitiveComponent* TraceForObject(FMatrix& OutWarp) const;

	/** The player's frame: eye position, view yaw, size, and a flipped Y axis while mirrored. */
	FMatrix GetPlayerFrame() const;
	FVector GetEyeLocation() const;
	FVector GetViewForward() const;
	double GetPlayerScale() const;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> Handle;

	UPROPERTY(Transient)
	TObjectPtr<UPortalTraversalComponent> OwnerTraversal;

	TWeakObjectPtr<UPrimitiveComponent> HeldComponent;
	TWeakObjectPtr<UPortalTraversalComponent> HeldTraversal;
	ECollisionResponse HeldPawnResponse = ECR_Block;

	/** Held object's orientation and size in the player's frame (no translation). */
	FMatrix HeldInFrame = FMatrix::Identity;
	/** Maps the player's side to the held object's side: the portals between them. */
	FMatrix HeldWarp = FMatrix::Identity;
	/** Eye to object centre, in units of the player's size. */
	double HeldDistance = 100.0;
	/**
	 * How far the object may be from its hold point before it's dropped, in units of the player's
	 * size. Starts large enough for the pull from where it was picked up, then shrinks to BreakDistance.
	 */
	double AllowedDistance = 0.0;
	/** Seconds the object has been further than AllowedDistance. */
	float StuckTime = 0.0f;
};
