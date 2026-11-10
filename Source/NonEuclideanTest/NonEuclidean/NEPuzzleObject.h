#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NEPuzzleObject.generated.h"

class UPortalTraversalComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A physics object for the portal puzzles: it can be carried and thrown (UPortalGrabComponent) and
 * passes through portals (UPortalTraversalComponent), so a scaling portal resizes it and a mirroring
 * portal flips its handedness. Sockets (ANEPuzzleSocket) check its kind, size and handedness.
 *
 * The mesh's pivot must be its centre (the point that crosses portals). Its simple collision
 * should be boxes, spheres or convex hulls, which work with a mirrored (negative) scale.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEPuzzleObject : public AActor
{
	GENERATED_BODY()

public:
	ANEPuzzleObject();

	/** Mesh at size 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	TObjectPtr<UStaticMesh> ObjectMesh;

	/** What sort of object this is; sockets accept one kind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	FName Kind = TEXT("Crate");

	/** Name shown on the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	FString Label = TEXT("Crate");

	/** Size relative to the mesh (1 = as built; portals change it). */
	UFUNCTION(BlueprintPure, Category = "Puzzle")
	float GetSize() const;

	/** True if the object is the mirror image of the mesh (an odd number of mirroring portals). */
	UFUNCTION(BlueprintPure, Category = "Puzzle")
	bool IsMirrored() const;

	/** True once a socket has locked it in place. */
	UFUNCTION(BlueprintPure, Category = "Puzzle")
	bool IsLocked() const { return bLocked; }

	/** Stops simulating and fixes the object at Transform (used by sockets). */
	void LockInto(const FTransform& Transform);

	/** Moves the object (e.g. for tests), optionally freezing it. */
	void PlaceAt(const FTransform& Transform, bool bFreeze);

	/** "size x0.50, mirrored" */
	FString DescribeState() const;

	UStaticMeshComponent* GetMesh() const { return Mesh; }
	UPortalTraversalComponent* GetPortalTraversal() const { return PortalTraversal; }

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPortalTraversalComponent> PortalTraversal;

	bool bLocked = false;
};
