#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PortalSubsystem.generated.h"

class APortal;
class APlayerController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FMinimalViewInfo;

/**
 * Renders all portals recursively (the technique of CodeParade's NonEuclidean engine, with scene
 * captures in place of OpenGL framebuffers).
 *
 * Every frame, after the camera has its final position (APortalPlayerCameraManager::UpdateCamera):
 *  1. Build a tree of views. The root is the player's view. For each portal visible in a view,
 *     a child view is made by warping that view's camera through the portal. Visibility is tested
 *     on the CPU (facing, clip plane, screen rectangle inside the parent portal's rectangle).
 *     Portals in views at depth MaxDepth-1 are drawn with the material's "Pink" colour.
 *  2. Capture the views deepest-first. Before each capture, every portal visible in that view is
 *     pointed at its child view's render target (or pink). The same portal can show different
 *     images in different views, which works because parameter updates and captures reach the
 *     render thread in the order they're issued.
 *  3. Point the portals in the player's view at the depth-1 targets.
 *
 * Every view uses the player's projection matrix and the viewport's aspect ratio, so the portal
 * material can sample its texture at screen-space UV. Each capture clips everything on the near
 * side of its exit portal (global clip plane: requires r.AllowGlobalClipPlane=1). Captures use
 * the player camera's post-process settings and are tonemapped once, by the player's view.
 *
 * Spaces: actors tagged "PortalSpace.<n>" belong to space n; untagged actors (sky, player) belong
 * to every space. Build alternative rooms far apart: each view (the player's included) renders only
 * its own space and looks only through portals in it, so the other rooms never show in the
 * distance. The player's space is the one whose bounds contain the camera. A portal tagged with one
 * space but standing in another's area is logged as a warning: it can't be seen from there.
 *
 * Mirroring: a view seen through a mirroring portal (see APortal) would need a mirrored camera,
 * which can't render. Each view keeps a mirrored flag instead and is captured with the matching
 * proper camera, whose image is the true view flipped left-right. Portal materials undo the
 * flip with their "MirrorU" parameter (parent view flipped XOR child view flipped), and while
 * the player is mirrored their own view is flipped on screen by a post-process material
 * (/PortalWorlds/Materials/M_PortalMirrorView, after tonemapping, so UI isn't affected). In a
 * world with mirroring portals that pass runs all the time (unflipped until it's needed).
 *
 * Console variables: Portal.MaxDepth, Portal.MaxCaptures, Portal.Debug (all pink, no captures),
 * Portal.Hide (no portal surfaces), Portal.ResolutionFalloff, Portal.DynamicNearPlane,
 * Portal.Spaces, Portal.ShowStats.
 */
UCLASS()
class PORTALWORLDS_API UPortalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPortalSubsystem();

	void RegisterPortal(APortal* Portal);
	void UnregisterPortal(APortal* Portal);

	/** Called once per frame with the player's final view. */
	void RenderPortals(APlayerController* PC, const FMinimalViewInfo& View);

	/**
	 * Reverses yaw (and roll) input while the player is mirrored, so turning right turns the
	 * flipped view right. APortalPlayerCameraManager calls this from ProcessViewRotation.
	 */
	static void MirrorViewRotationInput(const APlayerController* PC, FRotator& InOutDeltaRot);

	/** True if the player's view was shown mirrored in the last frame. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	bool IsViewMirrored() const { return bViewMirrored; }

	/** Distance from Point to the nearest portal rectangle, or a large number if there are none. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	double GetNearestPortalDistance(const FVector& Point) const;

	/** Portal captures rendered in the last frame. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	int32 GetLastCaptureCount() const { return LastCaptureCount; }

	const TArray<TWeakObjectPtr<APortal>>& GetPortals() const { return Portals; }

	/** All portals currently in play. */
	UFUNCTION(BlueprintPure, Category = "Portal", DisplayName = "Get Portals")
	TArray<APortal*> GetAllPortals() const;

	/** Tag prefix for spaces: an actor tagged "PortalSpace.2" is in space 2. */
	static const TCHAR* SpaceTagPrefix;

	/** Space of an actor from its "PortalSpace.<n>" tag, or -1 if it's in every space. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	static int32 GetActorSpace(const AActor* Actor);

	/**
	 * Puts an actor in a space by setting its "PortalSpace.<n>" tag (-1 removes it: every space).
	 * Spaces are rebuilt on the next frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetActorSpace(AActor* Actor, int32 Space);

	/** Space the player's camera is in, or -1 if the map doesn't use spaces. */
	UFUNCTION(BlueprintPure, Category = "Portal")
	int32 GetCurrentSpace() const { return CurrentSpace; }

	/** Re-reads every actor's space tag on the next frame. Call after spawning or re-tagging actors at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void RefreshSpaces() { bSpacesDirty = true; }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FChild
	{
		APortal* Portal = nullptr;
		int32 Node = INDEX_NONE; // INDEX_NONE: show pink
	};

	struct FViewNode
	{
		FVector Location;
		FRotator Rotation;
		FVector Forward, Right, Up;    // the true view's axes: Right is flipped for a mirrored view
		bool bMirrored = false;        // captured with Rotation, the true view flipped left-right
		FBox2D ScreenRect;             // region of the screen this view can show (0..1 UV)
		const APortal* ExitPortal = nullptr;
		FVector ClipBase = FVector::ZeroVector;
		FVector ClipNormal = FVector::ZeroVector; // keeps the side this points to
		int32 Depth = 0;
		int32 Space = INDEX_NONE;
		TArray<FChild> Children;
	};

	int32 BuildNode(const FVector& Location, const FRotator& Rotation, bool bMirrored, const APortal* ExitPortal, const FBox2D& ScreenRect, int32 Depth, int32 Space);
	void ApplyMirrorView(APlayerController* PC);
	bool ComputeScreenRect(const FViewNode& Node, const FVector Corners[4], FBox2D& OutRect) const;
	void CaptureNode(int32 NodeIndex);
	void EnsureCaptureResources(int32 NodeIndex);
	void ApplyNearPlane(const FVector& ViewLocation);
	void RebuildSpaces();
	void WarnAboutMisplacedPortals(const TMap<int32, FBox>& Geometry);
	void UpdatePlayerSpace(APlayerController* PC, const FVector& ViewLocation);

	struct FSpace
	{
		FBox Bounds = FBox(ForceInit);
		TArray<AActor*> HiddenActors; // actors of every other space
	};
	TMap<int32, FSpace> Spaces;
	bool bSpacesDirty = true;
	TSet<TObjectKey<APortal>> WarnedMisplacedPortals; // each misplaced portal is reported once
	bool bSpacesEnabled = true; // Portal.Spaces when the spaces were last built
	int32 CurrentSpace = INDEX_NONE;
	int32 PlayerHiddenSpace = MIN_int32; // space whose hidden list the player's view currently uses

	TArray<TWeakObjectPtr<APortal>> Portals;
	TArray<FViewNode> Nodes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CaptureOwner;

	/** Post-process material that flips the player's view left-right while mirrored. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MirrorViewMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MirrorViewInstance;

	FPostProcessSettings MirrorViewSettings;
	float MirrorViewParam = -1.0f;
	bool bViewMirrored = false;

	// Per-frame view parameters shared by every node.
	FMatrix ProjectionMatrix = FMatrix::Identity;
	double TanHalfFovX = 1.0;
	double TanHalfFovY = 1.0;
	FIntPoint ViewSize = FIntPoint(1280, 720);
	float NearPlane = 10.0f;
	FPostProcessSettings ViewPostProcess; // the player camera's, copied to every capture
	float ViewPostProcessWeight = 0.0f;

	int32 LastCaptureCount = 0;
	float DefaultNearPlane = -1.0f;
};
