#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Portal.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTexture;

/**
 * A seamless portal surface (based on CodeParade's NonEuclidean engine, MIT licence).
 *
 * The surface lies in the actor's local XZ plane, centred on the actor, spanning
 * +/-SurfaceHalfExtent before actor scale. Its "front" normal is local -Y. The default mesh is
 * /PortalWorlds/Meshes/SM_PortalQuad (a double-sided quad spanning +/-100 cm) with
 * /PortalWorlds/Materials/M_Portal. A replacement material must have a texture parameter
 * "PortalTex" sampled at screen-space UV (ScreenPosition.ViewportUV) and a scalar parameter "Pink"
 * (1 = flat colour, shown at the recursion limit), and should flip the sampled U when a scalar
 * parameter "MirrorU" is 1 (for mirroring portals); instancing M_Portal is easiest.
 *
 * Each side links to another portal. Looking or walking through either side warps space by
 *     Warp = this.LocalToWorld^-1 * Target.LocalToWorld
 * i.e. a point keeps its coordinates relative to the portal (no 180 degree flip), so entering
 * this portal's front leaves through the target's back. Rotate the target 180 degrees to come out
 * facing the other way. A size difference between the two actors (keep it uniform) scales
 * whatever passes through.
 *
 * Mirroring: give one of two linked portals a negative X scale (e.g. -1, 1, 1) and the warp
 * becomes a reflection across the portal's width. Everything seen through it is mirrored, and
 * whoever walks through comes out mirrored relative to the world (see UPortalTraversalComponent).
 * The geometry around the two portals should be left-right symmetric, or the seam will show.
 */
UCLASS()
class PORTALWORLDS_API APortal : public AActor
{
	GENERATED_BODY()

public:
	APortal();

	/** Portal reached by looking/walking through the front side (the side the normal points to). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Portal")
	TObjectPtr<APortal> FrontLink;

	/** Portal reached by looking/walking through the back side. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Portal")
	TObjectPtr<APortal> BackLink;

	/** Half size of the portal rectangle in local X (width) and Z (height), before actor scale. Must match the surface mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Portal")
	FVector2D SurfaceHalfExtent = FVector2D(100.0, 100.0);

	/** Links both sides of A to B and both sides of B to A. */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	static void Connect(APortal* A, APortal* B);

	/** Unit normal of the "front" side, in world space. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	FVector GetNormal() const;

	/** Signed distance from the portal plane (positive on the front side). */
	double SignedDistance(const FVector& Point) const;

	/** Link used for a viewer at Point, or null if Point lies on the plane. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	APortal* GetLinkFor(const FVector& Point) const;

	/** True if the segment From -> To crosses the portal rectangle, with the plane moved by Bump. */
	bool Intersects(const FVector& From, const FVector& To, const FVector& Bump) const;

	/** Distance from Point to the portal rectangle. */
	double DistanceTo(const FVector& Point) const;

	/** World-space corners of the portal rectangle. */
	void GetCorners(FVector OutCorners[4]) const;

	/** Matrix mapping world points seen through this portal into Target's space (row-vector convention). */
	FMatrix GetWarpTo(const APortal* Target) const;

	/** True if passing from this portal to Target mirrors space (one of the two has a negative scale). */
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsMirroredLinkTo(const APortal* Target) const;

	UStaticMeshComponent* GetSurface() const { return Surface; }

	/** Shows a captured view on the surface, flipped left-right if bMirrorU. */
	void ShowTexture(UTexture* Texture, bool bMirrorU = false);

	/** Shows the material's flat "Pink" colour (end of the render chain, or Portal.Debug). */
	void ShowPink();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> Surface;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	// Last values sent to the material, to skip redundant parameter updates. ShownTexture is only
	// compared, never dereferenced.
	const UTexture* ShownTexture = nullptr;
	bool bShowingPink = true; // M_Portal's default
	bool bShownMirrorU = false; // M_Portal's default
};
