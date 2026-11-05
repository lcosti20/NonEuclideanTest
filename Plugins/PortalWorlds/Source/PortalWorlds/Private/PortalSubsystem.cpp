#include "PortalSubsystem.h"

#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Camera/PlayerCameraManager.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "PortalTraversalComponent.h"
#include "Portal.h"
#include "PortalWorlds.h"
#include "RenderCore.h"
#include "SceneView.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMaxDepth(
		TEXT("Portal.MaxDepth"), 4,
		TEXT("Recursion depth: views up to depth MaxDepth-1 are captured, ")
		TEXT("and portals seen in the deepest views are drawn pink."));
	TAutoConsoleVariable<int32> CVarMaxCaptures(
		TEXT("Portal.MaxCaptures"), 32, TEXT("Upper limit on portal captures per frame; further portals are drawn pink."));
	TAutoConsoleVariable<bool> CVarDebug(
		TEXT("Portal.Debug"), false, TEXT("Draw every portal surface with its flat Pink colour and skip all captures."));
	TAutoConsoleVariable<bool> CVarHide(
		TEXT("Portal.Hide"), false, TEXT("Hide portal surfaces and skip all captures."));
	TAutoConsoleVariable<float> CVarResolutionFalloff(
		TEXT("Portal.ResolutionFalloff"), 1.0f,
		TEXT("Render-target resolution multiplier per recursion level beyond the first (1 = full resolution at every depth)."));
	TAutoConsoleVariable<bool> CVarDynamicNearPlane(
		TEXT("Portal.DynamicNearPlane"), true,
		TEXT("Near plane = clamp(distance to nearest portal / 2, 0.1 cm, 10 cm), so the camera can get right up to a portal. ")
		TEXT("Changes the engine's global near plane; 0 leaves it alone."));
	TAutoConsoleVariable<bool> CVarSpaces(
		TEXT("Portal.Spaces"), true,
		TEXT("Render only the current space in each view (actors tagged PortalSpace.<n>). 0 shows every space everywhere (debug)."));
	TAutoConsoleVariable<bool> CVarShowStats(
		TEXT("Portal.ShowStats"), false, TEXT("Show portal view/capture counts on screen."));

	constexpr double NearPlaneMin = 0.1;  // cm
	constexpr double NearPlaneMax = 10.0; // cm

	bool Intersect(const FBox2D& A, const FBox2D& B, FBox2D& Out)
	{
		Out.Min = FVector2D(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y));
		Out.Max = FVector2D(FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
		Out.bIsValid = Out.Min.X < Out.Max.X && Out.Min.Y < Out.Max.Y;
		return Out.bIsValid;
	}
}

const TCHAR* UPortalSubsystem::SpaceTagPrefix = TEXT("PortalSpace.");

UPortalSubsystem::UPortalSubsystem()
{
	// A hard reference from the constructor, like APortal's material, so the asset is cooked.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MirrorViewFinder(TEXT("/PortalWorlds/Materials/M_PortalMirrorView"));
	if (MirrorViewFinder.Succeeded())
	{
		MirrorViewMaterial = MirrorViewFinder.Object;
	}
}

void UPortalSubsystem::MirrorViewRotationInput(const APlayerController* PC, FRotator& InOutDeltaRot)
{
	const UPortalTraversalComponent* Traversal = UPortalTraversalComponent::FindForPlayer(PC);
	if (Traversal && Traversal->IsMirrored())
	{
		InOutDeltaRot.Yaw = -InOutDeltaRot.Yaw;
		InOutDeltaRot.Roll = -InOutDeltaRot.Roll;
	}
}

void UPortalSubsystem::ApplyMirrorView(APlayerController* PC)
{
	const UPortalTraversalComponent* Traversal = UPortalTraversalComponent::FindForPlayer(PC);
	bViewMirrored = Traversal && Traversal->IsMirrored();

	// The flip pass runs (as a no-op) from the start in any world with a mirroring portal: the
	// renderer's first use of a post-process material can show a broken frame, and that frame
	// should be the first one of the level, not the moment the player walks through.
	bool bNeedPass = bViewMirrored;
	for (int32 i = 0; i < Portals.Num() && !bNeedPass; ++i)
	{
		if (const APortal* Portal = Portals[i].Get())
		{
			bNeedPass = Portal->IsMirroredLinkTo(Portal->FrontLink) || Portal->IsMirroredLinkTo(Portal->BackLink);
		}
	}
	if (!bNeedPass || !PC->PlayerCameraManager)
	{
		return;
	}
	if (!MirrorViewMaterial)
	{
		if (bViewMirrored)
		{
			UE_LOG(LogPortalWorlds, Error, TEXT("/PortalWorlds/Materials/M_PortalMirrorView is missing: the mirrored view can't be shown"));
		}
		return;
	}
	if (!MirrorViewInstance)
	{
		MirrorViewInstance = UMaterialInstanceDynamic::Create(MirrorViewMaterial, this);
		MirrorViewSettings.WeightedBlendables.Array.Reset();
		MirrorViewSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, MirrorViewInstance));
		MirrorViewParam = -1.0f;
	}
	const float Param = bViewMirrored ? 1.0f : 0.0f;
	if (Param != MirrorViewParam)
	{
		MirrorViewInstance->SetScalarParameterValue(TEXT("Mirror"), Param);
		MirrorViewParam = Param;
	}
	// The camera manager clears its cached blends every update, so this lasts one frame.
	PC->PlayerCameraManager->AddCachedPPBlend(MirrorViewSettings, 1.0f);
}

void UPortalSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

#if WITH_EDITOR
	// Uncooked runs compile shaders on demand; without this the first mirrored frames would use a
	// fallback while M_PortalMirrorView compiles. Cooked games have it precompiled.
	if (MirrorViewMaterial)
	{
		if (FMaterialResource* Resource = MirrorViewMaterial->GetMaterialResource(GShaderPlatformForFeatureLevel[InWorld.GetFeatureLevel()]))
		{
			Resource->FinishCompilation();
		}
	}
#endif

	static bool bWarned = false;
	if (!bWarned)
	{
		bWarned = true;
		const IConsoleVariable* ClipPlane = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowGlobalClipPlane"));
		if (!ClipPlane || !ClipPlane->GetBool())
		{
			UE_LOG(LogPortalWorlds, Warning, TEXT("r.AllowGlobalClipPlane is off: portal views will show geometry between the ")
				TEXT("virtual camera and the exit portal. Enable 'Support global clip plane for Planar Reflections' in ")
				TEXT("Project Settings > Rendering (r.AllowGlobalClipPlane=True in DefaultEngine.ini) and restart."));
		}
		const IConsoleVariable* Occlusion = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowOcclusionQueries"));
		if (Occlusion && Occlusion->GetBool())
		{
			UE_LOG(LogPortalWorlds, Log, TEXT("r.AllowOcclusionQueries is on: geometry may pop for a frame in portal views ")
				TEXT("whose camera jumps. Set r.AllowOcclusionQueries=False if you see this."));
		}
	}
}

bool UPortalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPortalSubsystem::Deinitialize()
{
	if (DefaultNearPlane > 0.0f)
	{
		SetNearClipPlaneGlobals(DefaultNearPlane);
	}
	Super::Deinitialize();
}

void UPortalSubsystem::RegisterPortal(APortal* Portal)
{
	Portals.AddUnique(Portal);
	bSpacesDirty = true;
}

int32 UPortalSubsystem::GetActorSpace(const AActor* Actor)
{
	static const FString Prefix(SpaceTagPrefix);
	if (Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(Prefix))
			{
				return FCString::Atoi(*TagString.RightChop(Prefix.Len()));
			}
		}
	}
	return INDEX_NONE;
}

TArray<APortal*> UPortalSubsystem::GetAllPortals() const
{
	TArray<APortal*> Result;
	for (const TWeakObjectPtr<APortal>& Portal : Portals)
	{
		if (Portal.IsValid())
		{
			Result.Add(Portal.Get());
		}
	}
	return Result;
}

void UPortalSubsystem::SetActorSpace(AActor* Actor, int32 Space)
{
	if (!Actor)
	{
		return;
	}
	const FString Prefix(SpaceTagPrefix);
	Actor->Tags.RemoveAll([&Prefix](const FName& Tag) { return Tag.ToString().StartsWith(Prefix); });
	if (Space >= 0)
	{
		Actor->Tags.Add(FName(*FString::Printf(TEXT("%s%d"), SpaceTagPrefix, Space)));
	}
	bSpacesDirty = true;
}

void UPortalSubsystem::RebuildSpaces()
{
	bSpacesDirty = false;
	Spaces.Reset();
	PlayerHiddenSpace = MIN_int32;
	bSpacesEnabled = CVarSpaces.GetValueOnGameThread();
	if (!bSpacesEnabled)
	{
		return;
	}
	TMap<int32, TArray<AActor*>> Members;
	TMap<int32, FBox> Geometry; // each space's bounds without its portals, for WarnAboutMisplacedPortals
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const int32 Space = GetActorSpace(*It);
		if (Space != INDEX_NONE)
		{
			const FBox Box = It->GetComponentsBoundingBox(/*bNonColliding*/ true);
			Members.FindOrAdd(Space).Add(*It);
			Spaces.FindOrAdd(Space).Bounds += Box;
			if (!It->IsA<APortal>())
			{
				Geometry.FindOrAdd(Space, FBox(ForceInit)) += Box;
			}
		}
	}
	WarnAboutMisplacedPortals(Geometry);
	for (TPair<int32, FSpace>& Pair : Spaces)
	{
		for (const TPair<int32, TArray<AActor*>>& Other : Members)
		{
			if (Other.Key != Pair.Key)
			{
				Pair.Value.HiddenActors.Append(Other.Value);
			}
		}
	}
	PlayerHiddenSpace = MIN_int32;
}

void UPortalSubsystem::WarnAboutMisplacedPortals(const TMap<int32, FBox>& Geometry)
{
	// Same margin as the player's space test in UpdatePlayerSpace.
	auto Inside = [&Geometry](int32 Space, const FVector& Location)
	{
		const FBox* Box = Geometry.Find(Space);
		return Box && Box->IsValid && Box->ExpandBy(100.0).IsInsideXY(Location);
	};
	for (const TWeakObjectPtr<APortal>& PortalPtr : Portals)
	{
		const APortal* Portal = PortalPtr.Get();
		const int32 Space = GetActorSpace(Portal);
		if (!Portal || Space == INDEX_NONE || Inside(Space, Portal->GetActorLocation()))
		{
			continue;
		}
		for (const TPair<int32, FBox>& Other : Geometry)
		{
			if (Other.Key != Space && Inside(Other.Key, Portal->GetActorLocation()))
			{
				bool bAlreadyWarned = false;
				WarnedMisplacedPortals.Add(Portal, &bAlreadyWarned);
				if (!bAlreadyWarned)
				{
					UE_LOG(LogPortalWorlds, Warning, TEXT("Portal %s is tagged %s%d but stands in space %d's area. Players in ")
						TEXT("space %d won't see it or anything through it (other spaces are hidden), but can still walk ")
						TEXT("through it. Tag it %s%d, or move it back into space %d."),
						*Portal->GetActorNameOrLabel(), SpaceTagPrefix, Space, Other.Key, Other.Key, SpaceTagPrefix, Other.Key, Space);
				}
				break;
			}
		}
	}
}

void UPortalSubsystem::UpdatePlayerSpace(APlayerController* PC, const FVector& ViewLocation)
{
	if (Spaces.IsEmpty())
	{
		CurrentSpace = INDEX_NONE;
		if (PlayerHiddenSpace != INDEX_NONE)
		{
			PlayerHiddenSpace = INDEX_NONE;
			PC->HiddenActors.Reset();
		}
		return;
	}
	auto Contains = [&ViewLocation](const FSpace& Space)
	{
		return Space.Bounds.ExpandBy(100.0).IsInsideXY(ViewLocation);
	};
	const FSpace* Current = Spaces.Find(CurrentSpace);
	if (!Current || !Contains(*Current))
	{
		for (const TPair<int32, FSpace>& Pair : Spaces)
		{
			if (Contains(Pair.Value))
			{
				CurrentSpace = Pair.Key;
				break;
			}
		}
	}
	if (PlayerHiddenSpace != CurrentSpace)
	{
		PlayerHiddenSpace = CurrentSpace;
		PC->HiddenActors.Reset();
		if (const FSpace* Space = Spaces.Find(CurrentSpace))
		{
			PC->HiddenActors.Append(Space->HiddenActors);
		}
	}
}

void UPortalSubsystem::UnregisterPortal(APortal* Portal)
{
	Portals.Remove(Portal);
}

double UPortalSubsystem::GetNearestPortalDistance(const FVector& Point) const
{
	double Nearest = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<APortal>& Portal : Portals)
	{
		if (Portal.IsValid())
		{
			Nearest = FMath::Min(Nearest, Portal->DistanceTo(Point));
		}
	}
	return Nearest;
}

void UPortalSubsystem::ApplyNearPlane(const FVector& ViewLocation)
{
	if (DefaultNearPlane < 0.0f)
	{
		DefaultNearPlane = GNearClippingPlane;
	}
	NearPlane = CVarDynamicNearPlane.GetValueOnGameThread()
		? static_cast<float>(FMath::Clamp(GetNearestPortalDistance(ViewLocation) * 0.5, NearPlaneMin, NearPlaneMax))
		: DefaultNearPlane;
	if (NearPlane != GNearClippingPlane)
	{
		SetNearClipPlaneGlobals(NearPlane);
	}
}

void UPortalSubsystem::RenderPortals(APlayerController* PC, const FMinimalViewInfo& View)
{
	LastCaptureCount = 0;
	Nodes.Reset();
	Portals.RemoveAll([](const TWeakObjectPtr<APortal>& P) { return !P.IsValid(); });
	if (!PC)
	{
		return;
	}
	ApplyMirrorView(PC);
	if (Portals.IsEmpty())
	{
		return;
	}

	const bool bHide = CVarHide.GetValueOnGameThread();
	for (const TWeakObjectPtr<APortal>& Portal : Portals)
	{
		Portal->GetSurface()->SetVisibility(!bHide);
	}

	if (bSpacesDirty || CVarSpaces.GetValueOnGameThread() != bSpacesEnabled)
	{
		RebuildSpaces();
	}
	UpdatePlayerSpace(PC, View.Location);

	ApplyNearPlane(View.Location);
	if (bHide)
	{
		return;
	}
	if (CVarDebug.GetValueOnGameThread())
	{
		for (const TWeakObjectPtr<APortal>& Portal : Portals)
		{
			Portal->ShowPink();
		}
		return;
	}

	// The player's exact projection (FOV, aspect constraint, near plane) for every capture.
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer || !LocalPlayer->ViewportClient || !LocalPlayer->ViewportClient->Viewport)
	{
		return;
	}
	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
	{
		return;
	}
	ProjectionMatrix = ProjectionData.ProjectionMatrix;
	ViewPostProcess = View.PostProcessSettings;
	ViewPostProcessWeight = View.PostProcessBlendWeight;
	TanHalfFovX = 1.0 / ProjectionMatrix.M[0][0];
	TanHalfFovY = 1.0 / ProjectionMatrix.M[1][1];
	ViewSize = ProjectionData.GetConstrainedViewRect().Size();
	if (ViewSize.X <= 0 || ViewSize.Y <= 0)
	{
		return;
	}

	BuildNode(View.Location, View.Rotation, bViewMirrored, nullptr, FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0)), 0, CurrentSpace);
	CaptureNode(0);

	if (CVarShowStats.GetValueOnGameThread() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(reinterpret_cast<UPTRINT>(this)), 0.0f, FColor::Yellow,
			FString::Printf(TEXT("Portals: %d registered, %d views, %d captures, near plane %.2f cm%s"),
				Portals.Num(), Nodes.Num(), LastCaptureCount, NearPlane, bViewMirrored ? TEXT(", mirrored") : TEXT("")));
	}
}

int32 UPortalSubsystem::BuildNode(const FVector& Location, const FRotator& Rotation, bool bMirrored, const APortal* ExitPortal, const FBox2D& ScreenRect, int32 Depth, int32 Space)
{
	const int32 Index = Nodes.AddDefaulted();
	{
		FViewNode& Node = Nodes[Index];
		Node.Location = Location;
		Node.Rotation = Rotation;
		const FRotationMatrix R(Rotation);
		Node.Forward = R.GetUnitAxis(EAxis::X);
		Node.Right = R.GetUnitAxis(EAxis::Y) * (bMirrored ? -1.0 : 1.0);
		Node.bMirrored = bMirrored;
		Node.Up = R.GetUnitAxis(EAxis::Z);
		Node.ScreenRect = ScreenRect;
		Node.ExitPortal = ExitPortal;
		Node.Depth = Depth;
		Node.Space = Space;
		if (ExitPortal)
		{
			// Keep only what lies beyond the exit portal. The plane is pulled towards the camera by
			// the near-plane distance so geometry touching the portal (door frames) isn't clipped.
			FVector Normal = ExitPortal->GetNormal();
			if (FVector::DotProduct(Location - ExitPortal->GetActorLocation(), Normal) > 0.0)
			{
				Normal = -Normal;
			}
			Node.ClipNormal = Normal;
			Node.ClipBase = ExitPortal->GetActorLocation() - Normal * NearPlane;
		}
	}

	const int32 MaxDepth = FMath::Max(1, CVarMaxDepth.GetValueOnGameThread());
	const int32 MaxCaptures = FMath::Max(0, CVarMaxCaptures.GetValueOnGameThread());

	for (const TWeakObjectPtr<APortal>& PortalPtr : Portals)
	{
		APortal* Portal = PortalPtr.Get();
		if (!Portal || Portal == ExitPortal)
		{
			continue;
		}
		const int32 PortalSpace = GetActorSpace(Portal);
		if (Space != INDEX_NONE && PortalSpace != INDEX_NONE && PortalSpace != Space)
		{
			continue; // belongs to another space, which this view doesn't render
		}

		const FViewNode& Node = Nodes[Index]; // re-fetched each iteration: recursion grows Nodes
		const double Side = Portal->SignedDistance(Node.Location);
		if (FMath::IsNearlyZero(Side))
		{
			continue;
		}

		FVector Corners[4];
		Portal->GetCorners(Corners);
		if (ExitPortal)
		{
			bool bAnyKept = false;
			for (const FVector& C : Corners)
			{
				bAnyKept |= FVector::DotProduct(C - Node.ClipBase, Node.ClipNormal) >= 0.0;
			}
			if (!bAnyKept)
			{
				continue; // entirely on the clipped side of this view's exit portal
			}
		}

		FBox2D Rect;
		if (!ComputeScreenRect(Node, Corners, Rect) || !Intersect(Rect, Node.ScreenRect, Rect))
		{
			continue; // not visible in this view
		}

		FChild Child;
		Child.Portal = Portal;
		APortal* Target = Side > 0.0 ? Portal->FrontLink : Portal->BackLink;
		if (Target && Depth + 1 < MaxDepth && Nodes.Num() - 1 < MaxCaptures)
		{
			const FMatrix Warp = Portal->GetWarpTo(Target);
			const FVector NewLocation = Warp.TransformPosition(Node.Location);
			const FVector NewForward = Warp.TransformVector(Node.Forward).GetSafeNormal();
			const FVector NewUp = Warp.TransformVector(Node.Up).GetSafeNormal();
			// The camera is always a proper rotation; a reflection in the warp flips the view instead.
			const FRotator NewRotation = FRotationMatrix::MakeFromXZ(NewForward, NewUp).Rotator();
			const bool bNewMirrored = Node.bMirrored != (Warp.Determinant() < 0.0);
			Child.Node = BuildNode(NewLocation, NewRotation, bNewMirrored, Target, Rect, Depth + 1, GetActorSpace(Target));
		}
		Nodes[Index].Children.Add(Child);
	}
	return Index;
}

bool UPortalSubsystem::ComputeScreenRect(const FViewNode& Node, const FVector Corners[4], FBox2D& OutRect) const
{
	FBox2D Rect(ForceInit);
	int32 Behind = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Rel = Corners[i] - Node.Location;
		const double Z = FVector::DotProduct(Rel, Node.Forward);
		if (Z <= KINDA_SMALL_NUMBER)
		{
			++Behind;
			continue;
		}
		const double U = 0.5 + 0.5 * FVector::DotProduct(Rel, Node.Right) / (Z * TanHalfFovX);
		const double V = 0.5 - 0.5 * FVector::DotProduct(Rel, Node.Up) / (Z * TanHalfFovY);
		Rect += FVector2D(U, V);
	}
	if (Behind == 4)
	{
		return false;
	}
	// A rectangle crossing the camera plane can cover any part of the screen.
	OutRect = Behind > 0 ? FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0)) : Rect;
	return true;
}

void UPortalSubsystem::CaptureNode(int32 NodeIndex)
{
	// Deepest views first, so every capture finds its children's images already rendered.
	for (int32 i = 0; i < Nodes[NodeIndex].Children.Num(); ++i)
	{
		const int32 ChildNode = Nodes[NodeIndex].Children[i].Node;
		if (ChildNode != INDEX_NONE)
		{
			CaptureNode(ChildNode);
		}
	}

	// Point the portals visible in this view at their images for this view.
	for (const FChild& Child : Nodes[NodeIndex].Children)
	{
		if (Child.Node == INDEX_NONE)
		{
			Child.Portal->ShowPink();
		}
		else
		{
			// The image is sampled in this view's rendered (possibly flipped) screen space.
			Child.Portal->ShowTexture(Targets[Child.Node], Nodes[NodeIndex].bMirrored != Nodes[Child.Node].bMirrored);
		}
	}

	if (NodeIndex == 0)
	{
		return; // the player's own view is rendered by the engine
	}

	EnsureCaptureResources(NodeIndex);
	const FViewNode& Node = Nodes[NodeIndex];
	USceneCaptureComponent2D* Capture = Captures[NodeIndex];
	Capture->SetWorldLocationAndRotation(Node.Location, Node.Rotation);
	Capture->bUseCustomProjectionMatrix = true;
	Capture->CustomProjectionMatrix = ProjectionMatrix;
	Capture->FOVAngle = FMath::RadiansToDegrees(2.0 * FMath::Atan(TanHalfFovX)); // used for culling only
	Capture->bEnableClipPlane = true;
	Capture->ClipPlaneBase = Node.ClipBase;
	Capture->ClipPlaneNormal = Node.ClipNormal;
	Capture->HiddenComponents.Reset();
	Capture->HiddenComponents.Add(Node.ExitPortal->GetSurface()); // the exit portal never shows itself
	Capture->HiddenActors.Reset();
	if (const FSpace* Space = Spaces.Find(Node.Space))
	{
		Capture->HiddenActors.Append(Space->HiddenActors);
	}
	Capture->PostProcessSettings = ViewPostProcess;
	Capture->PostProcessBlendWeight = ViewPostProcessWeight;
	Capture->TextureTarget = Targets[NodeIndex];
	Capture->CaptureScene();
	++LastCaptureCount;
}

void UPortalSubsystem::EnsureCaptureResources(int32 NodeIndex)
{
	UWorld* World = GetWorld();
	if (!CaptureOwner)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.Name = TEXT("PortalCaptures");
		CaptureOwner = World->SpawnActor<AActor>(Params);
		USceneComponent* Root = NewObject<USceneComponent>(CaptureOwner, TEXT("Root"));
		CaptureOwner->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	if (Captures.Num() <= NodeIndex)
	{
		Captures.SetNum(NodeIndex + 1);
		Targets.SetNum(NodeIndex + 1);
	}

	if (!Captures[NodeIndex])
	{
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureOwner);
		Capture->SetupAttachment(CaptureOwner->GetRootComponent());
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = false;
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; // tonemapped once, by the player's view
		Capture->ShowFlags.SetMotionBlur(false); // a capture has no history to blur or accumulate against
		Capture->ShowFlags.SetTemporalAA(false);
		Capture->RegisterComponent();
		Captures[NodeIndex] = Capture;
		UE_LOG(LogPortalWorlds, Verbose, TEXT("Capture %d created"), NodeIndex);
	}

	const double Scale = FMath::Pow(FMath::Clamp(CVarResolutionFalloff.GetValueOnGameThread(), 0.1f, 1.0f),
		FMath::Max(0, Nodes[NodeIndex].Depth - 1));
	const int32 Width = FMath::Max(16, FMath::RoundToInt(ViewSize.X * Scale));
	const int32 Height = FMath::Max(16, FMath::RoundToInt(ViewSize.Y * Scale));
	TObjectPtr<UTextureRenderTarget2D>& Target = Targets[NodeIndex];
	if (!Target)
	{
		Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Black;
		Target->bAutoGenerateMips = false;
		Target->InitAutoFormat(Width, Height);
		UE_LOG(LogPortalWorlds, Verbose, TEXT("Render target %d created (%dx%d)"), NodeIndex, Width, Height);
	}
	else if (Target->SizeX != Width || Target->SizeY != Height)
	{
		Target->ResizeTarget(Width, Height);
		UE_LOG(LogPortalWorlds, Verbose, TEXT("Render target %d resized (%dx%d)"), NodeIndex, Width, Height);
	}
}
