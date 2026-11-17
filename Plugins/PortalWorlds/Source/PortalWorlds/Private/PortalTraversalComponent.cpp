#include "PortalTraversalComponent.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Portal.h"
#include "PortalSubsystem.h"
#include "PortalWorlds.h"

namespace
{
	constexpr double BumpDistance = 2.0 * 0.1; // twice the minimum near plane (cm)

	TAutoConsoleVariable<bool> CVarClipObjects(
		TEXT("Portal.ClipObjects"), true,
		TEXT("Cut objects (and their crossing copies) at the portal plane while they're partway through (bClipAtPortal). 0 turns it off everywhere."));
}

UPortalTraversalComponent::UPortalTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after character movement
}

void UPortalTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Eye)
	{
		UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
		Eye = Camera ? static_cast<USceneComponent*>(Camera) : GetOwner()->GetRootComponent();
	}
	if (UCharacterMovementComponent* Movement = GetOwner()->FindComponentByClass<UCharacterMovementComponent>())
	{
		AddTickPrerequisiteComponent(Movement);
	}

	// Controller (input) -> InputMirrorTick -> pawn movement. See MirrorPendingInput.
	InputMirrorTick.Target = this;
	InputMirrorTick.TickGroup = TG_PrePhysics;
	InputMirrorTick.bCanEverTick = true;
	InputMirrorTick.bStartWithTickEnabled = true;
	InputMirrorTick.RegisterTickFunction(GetOwner()->GetLevel());
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
		{
			Movement->PrimaryComponentTick.AddPrerequisite(this, InputMirrorTick);
		}
	}
	UpdateInputTickPrerequisites();
}

void UPortalTraversalComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UPortalSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UPortalSubsystem>() : nullptr)
	{
		Subsystem->SetActorClippedAtPortal(GetOwner(), nullptr, FVector::ZeroVector);
		Subsystem->SetActorClippedAtPortal(CrossingCopy, nullptr, FVector::ZeroVector);
	}
	StopIgnoringPawns();
	if (CrossingCopy)
	{
		CrossingCopy->Destroy();
		CrossingCopy = nullptr;
	}
	if (InputMirrorTick.IsTickFunctionRegistered())
	{
		InputMirrorTick.UnRegisterTickFunction();
	}
	Super::EndPlay(Reason);
}

void FPortalInputMirrorTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (IsValid(Target))
	{
		Target->MirrorPendingInput();
	}
}

void UPortalTraversalComponent::UpdateInputTickPrerequisites()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (Controller == InputTickController.Get())
	{
		return;
	}
	if (AController* Old = InputTickController.Get())
	{
		InputMirrorTick.RemovePrerequisite(Old, Old->PrimaryActorTick);
	}
	if (Controller)
	{
		InputMirrorTick.AddPrerequisite(Controller, Controller->PrimaryActorTick);
	}
	InputTickController = Controller;
}

void UPortalTraversalComponent::MirrorPendingInput()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!bMirrored || !bMirrorMovementInput || !Pawn)
	{
		return;
	}
	const FVector Input = Pawn->Internal_GetPendingMovementInputVector();
	if (Input.IsNearlyZero())
	{
		return;
	}
	// Reflect across the vertical plane through the view direction: the screen is flipped left-right.
	const AController* Controller = Pawn->GetController();
	const FRotator Yaw(0.0, Controller ? Controller->GetControlRotation().Yaw : Pawn->GetActorRotation().Yaw, 0.0);
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	Pawn->Internal_ConsumeMovementInputVector();
	Pawn->Internal_AddMovementInput(Input - Right * (2.0 * FVector::DotProduct(Input, Right)), /*bForce*/ true);
}

void UPortalTraversalComponent::RefreshMirroredRendering(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	TArray<UPrimitiveComponent*> Primitives;
	Actor->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->MarkRenderStateDirty(); // recreated before this frame renders
	}
}

UPortalTraversalComponent* UPortalTraversalComponent::FindForPlayer(const APlayerController* PC)
{
	if (!PC)
	{
		return nullptr;
	}
	if (const AActor* ViewTarget = PC->GetViewTarget())
	{
		if (UPortalTraversalComponent* Found = ViewTarget->FindComponentByClass<UPortalTraversalComponent>())
		{
			return Found;
		}
	}
	return PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UPortalTraversalComponent>() : nullptr;
}

void UPortalTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateInputTickPrerequisites(); // the pawn may have been (re)possessed

	const UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>();
	if (!Eye || !Subsystem)
	{
		return;
	}

	const FVector Current = Eye->GetComponentLocation();
	if (bHasPreviousEye)
	{
		const double Bump = BumpDistance * GetOwner()->GetActorScale3D().Z;
		for (const TWeakObjectPtr<APortal>& PortalPtr : Subsystem->GetPortals())
		{
			APortal* Portal = PortalPtr.Get();
			if (!Portal)
			{
				continue;
			}
			const double Side = Portal->SignedDistance(PreviousEye);
			const FVector BumpVector = Portal->GetNormal() * (Side >= 0.0 ? Bump : -Bump);
			if (!Portal->Intersects(PreviousEye, Current, BumpVector))
			{
				continue;
			}
			if (APortal* Target = Side >= 0.0 ? Portal->FrontLink : Portal->BackLink)
			{
				Traverse(Portal, Target, BumpVector);
				break; // at most one portal per frame
			}
		}
	}
	PreviousEye = Eye->GetComponentLocation();
	bHasPreviousEye = true;

	UpdateCrossingCopy();
}

void UPortalTraversalComponent::ApplyScaleFactor(double Factor)
{
	AActor* Owner = GetOwner();
	const float F = static_cast<float>(Factor);
	Owner->SetActorScale3D(Owner->GetActorScale3D() * F); // scales about the actor origin (capsule centre)

	if (UCharacterMovementComponent* Movement = Owner->FindComponentByClass<UCharacterMovementComponent>())
	{
		Movement->MaxWalkSpeed *= F;
		Movement->MaxWalkSpeedCrouched *= F;
		Movement->MaxAcceleration *= F;
		Movement->BrakingDecelerationWalking *= F;
		Movement->BrakingDecelerationFalling *= F;
		Movement->MaxStepHeight *= F;
		Movement->JumpZVelocity *= F;
		Movement->GravityScale *= F;
	}
}

void UPortalTraversalComponent::Traverse(APortal* From, APortal* To, const FVector& Bump)
{
	AActor* Owner = GetOwner();
	const FMatrix Warp = From->GetWarpTo(To);
	if (!Cast<APawn>(Owner))
	{
		TraverseObject(From, To, Warp, Bump);
		return;
	}
	const double ScaleFactor = Warp.TransformVector(FVector::XAxisVector).Size();
	const bool bMirrorWarp = Warp.Determinant() < 0.0;

	// Position: warp the actor, moved twice the bump past the plane (the camera's offset from the
	// actor is vertical, so it survives any yaw/scale warp).
	const FVector NewLocation = Warp.TransformPosition(Owner->GetActorLocation() - Bump * 2.0);

	// Rotation: warp the view's forward and up vectors. Rebuilding the rotation from those keeps
	// it a proper rotation; a mirroring warp's reflection is carried by bMirrored instead.
	APawn* Pawn = Cast<APawn>(Owner);
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (Controller)
	{
		const FRotationMatrix View(Controller->GetControlRotation());
		const FVector Forward = Warp.TransformVector(View.GetUnitAxis(EAxis::X)).GetSafeNormal();
		const FVector Up = Warp.TransformVector(View.GetUnitAxis(EAxis::Z)).GetSafeNormal();
		Controller->SetControlRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());
	}

	Owner->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	if (Controller)
	{
		Owner->SetActorRotation(FRotator(0.0, Controller->GetControlRotation().Yaw, 0.0), ETeleportType::TeleportPhysics);
	}

	// Velocity (the warp's scale carries over).
	if (UCharacterMovementComponent* Movement = Owner->FindComponentByClass<UCharacterMovementComponent>())
	{
		Movement->Velocity = Warp.TransformVector(Movement->Velocity);
	}

	// A smaller or larger exit portal resizes the traveller.
	if (!FMath::IsNearlyEqual(ScaleFactor, 1.0, 1e-4))
	{
		ApplyScaleFactor(ScaleFactor);
	}

	if (bMirrorWarp)
	{
		bMirrored = !bMirrored;
	}

	// Temporal effects must not blend across the jump.
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}

	// Objects partway through portals decide in their own tick which pawns they collide with, and
	// that may already have run this frame. Coming out of a portal can put the pawn inside the
	// invisible part of one (an object or its copy), which must ignore it before it moves again.
	if (UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>())
	{
		TArray<AActor*> Clipped;
		Subsystem->GetClippedActors(Clipped);
		for (AActor* Actor : Clipped)
		{
			UPortalTraversalComponent* Other = Actor->FindComponentByClass<UPortalTraversalComponent>();
			if (Other && Other != this)
			{
				Other->UpdateCrossingCopy();
			}
		}
	}

	++TraversalCount;
	UE_LOG(LogPortalWorlds, Log, TEXT("Traversed %s -> %s (scale x%.3f%s), eye now %s"),
		*From->GetActorNameOrLabel(), *To->GetActorNameOrLabel(), ScaleFactor,
		bMirrorWarp ? (bMirrored ? TEXT(", now mirrored") : TEXT(", no longer mirrored")) : TEXT(""),
		*Eye->GetComponentLocation().ToString());
	OnPortalTraversed.Broadcast(From, To, static_cast<float>(ScaleFactor));
}

void UPortalTraversalComponent::TraverseObject(APortal* From, APortal* To, const FMatrix& Warp, const FVector& Bump)
{
	AActor* Owner = GetOwner();
	const double ScaleFactor = Warp.TransformVector(FVector::XAxisVector).Size();
	const bool bMirrorWarp = Warp.Determinant() < 0.0;

	// The whole transform goes through the warp. FTransform keeps a mirroring (negative
	// determinant) as a negative X scale, so a mirrored object's shape really is mirrored.
	const FTransform Old = Owner->GetActorTransform();
	FTransform New(Old.ToMatrixWithScale() * Warp);
	New.SetLocation(Warp.TransformPosition(Old.GetLocation() - Bump * 2.0));

	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	const bool bSimulating = Body && Body->IsSimulatingPhysics();
	const FVector LinearVelocity = bSimulating ? Body->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	const FVector AngularVelocity = bSimulating ? Body->GetPhysicsAngularVelocityInRadians() : FVector::ZeroVector;

	Owner->SetActorTransform(New, false, nullptr, ETeleportType::TeleportPhysics);

	if (bSimulating)
	{
		Body->SetPhysicsLinearVelocity(Warp.TransformVector(LinearVelocity));
		// Angular velocity is an axial vector: it turns with the warp's rotation, isn't scaled,
		// and reverses under a reflection.
		const FVector Spin = Warp.TransformVector(AngularVelocity) / ScaleFactor;
		Body->SetPhysicsAngularVelocityInRadians(bMirrorWarp ? -Spin : Spin);
		if (!FMath::IsNearlyEqual(ScaleFactor, 1.0, 1e-4))
		{
			if (FBodyInstance* Instance = Body->GetBodyInstance())
			{
				Instance->UpdateMassProperties(); // the shapes were rescaled; the mass wasn't
			}
		}
	}

	if (bMirrorWarp)
	{
		bMirrored = !bMirrored;
		RefreshMirroredRendering(Owner);
	}

	// An object in a space follows the portal into the exit's space.
	UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>();
	const int32 OwnerSpace = UPortalSubsystem::GetActorSpace(Owner);
	const int32 ToSpace = UPortalSubsystem::GetActorSpace(To);
	if (Subsystem && OwnerSpace != INDEX_NONE && ToSpace != INDEX_NONE && ToSpace != OwnerSpace)
	{
		Subsystem->SetActorSpace(Owner, ToSpace);
	}

	++TraversalCount;
	UE_LOG(LogPortalWorlds, Log, TEXT("Object %s traversed %s -> %s (scale x%.3f%s), now at %s, scale %s"),
		*Owner->GetActorNameOrLabel(), *From->GetActorNameOrLabel(), *To->GetActorNameOrLabel(), ScaleFactor,
		bMirrorWarp ? (bMirrored ? TEXT(", now mirrored") : TEXT(", no longer mirrored")) : TEXT(""),
		*Owner->GetActorLocation().ToString(), *Owner->GetActorScale3D().ToString());
	OnPortalTraversed.Broadcast(From, To, static_cast<float>(ScaleFactor));
}

AActor* UPortalTraversalComponent::GetOrCreateCrossingCopy()
{
	if (CrossingCopy)
	{
		return CrossingCopy;
	}
	AActor* Owner = GetOwner();
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CrossingCopy = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Owner->GetActorTransform(), Params);
	if (!CrossingCopy)
	{
		return nullptr;
	}
#if WITH_EDITOR
	CrossingCopy->SetActorLabel(Owner->GetActorNameOrLabel() + TEXT("_PortalCopy"));
#endif
	USceneComponent* Root = NewObject<USceneComponent>(CrossingCopy, TEXT("Root"));
	CrossingCopy->SetRootComponent(Root);
	Root->RegisterComponent();

	// Each static mesh at the same place relative to the actor (worked out with matrices: FTransform
	// can't represent every combination of negative scale and rotation).
	const FMatrix ToOwnerLocal = Owner->GetActorTransform().ToInverseMatrixWithScale();
	TArray<UStaticMeshComponent*> Meshes;
	Owner->GetComponents(Meshes);
	for (const UStaticMeshComponent* Source : Meshes)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(CrossingCopy);
		Mesh->SetStaticMesh(Source->GetStaticMesh());
		for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
		{
			Mesh->SetMaterial(Index, Source->GetMaterial(Index));
		}
		// Solid to pawns only (query collision, for character movement sweeps): nothing else should
		// treat it as a second object. Its pawn response is set while it's shown (SetCopyPawnResponse),
		// and its hits are passed on to the real object (OnCopyHit).
		Mesh->SetCollisionEnabled(Source->IsCollisionEnabled() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->OnComponentHit.AddDynamic(this, &UPortalTraversalComponent::OnCopyHit);
		Mesh->SetCastShadow(Source->CastShadow);
		Mesh->SetupAttachment(Root);
		Mesh->SetRelativeTransform(FTransform(Source->GetComponentTransform().ToMatrixWithScale() * ToOwnerLocal));
		Mesh->RegisterComponent();
		CrossingCopy->AddInstanceComponent(Mesh);
	}
	CrossingCopy->SetActorHiddenInGame(true);
	CrossingCopy->SetActorEnableCollision(false);
	CopyPawnResponse = ECR_Ignore;
	bCrossingCopyMirrored = Owner->GetActorTransform().ToMatrixWithScale().Determinant() < 0.0;
	return CrossingCopy;
}

namespace
{
#if WITH_EDITORONLY_DATA
	/**
	 * Why a material can't be cut at the portal, or an empty string if it can: its base material (instances
	 * inherit the parent's graph) has to call MF_PortalClip, and the material has to render as Masked.
	 * GetBlendMode() is what the renderer uses: it reports Opaque for a Masked material whose saved
	 * bCanMaskedBeAssumedOpaque flag is set (which a duplicate of an old opaque material can inherit),
	 * and the mask is then ignored.
	 */
	FString WhyMaterialIgnoresPortalClip(const UMaterialInterface* Material)
	{
		const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
		if (!Base)
		{
			return TEXT("no material");
		}
		bool bUsesClip = false;
		Base->IterateDependentFunctions([&bUsesClip](UMaterialFunctionInterface* Function)
		{
			bUsesClip = Function && Function->GetFName() == TEXT("MF_PortalClip");
			return !bUsesClip; // keep going until found
		});
		if (!bUsesClip)
		{
			return TEXT("doesn't call MF_PortalClip");
		}
		if (Material->GetBlendMode() != BLEND_Masked)
		{
			return Base->BlendMode == BLEND_Masked
				? TEXT("set to Masked but renders as Opaque because the engine assumes the mask is always opaque; recreate the base material instead of duplicating an opaque one")
				: TEXT("not Masked");
		}
		return FString();
	}
#endif

	/** A plane keeping the points P with dot(P, Normal) >= W, packed as (Normal, W) for MF_PortalClip. */
	FVector4f MakeClipPlane(const FVector& KeepNormal, const FVector& PointOnPlane)
	{
		return FVector4f(FVector3f(KeepNormal), static_cast<float>(FVector::DotProduct(KeepNormal, PointOnPlane)));
	}
}

void UPortalTraversalComponent::SetClipPlane(AActor* Actor, const FVector4f& Plane, FVector4f& InOutCurrent)
{
	if (!Actor || Plane == InOutCurrent)
	{
		return;
	}
	InOutCurrent = Plane;
	TArray<UPrimitiveComponent*> Primitives;
	Actor->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetCustomPrimitiveDataVector4f(ClipPlaneDataIndex, Plane);
	}
}

void UPortalTraversalComponent::WarnIfMaterialsIgnoreClip() const
{
#if WITH_EDITORONLY_DATA
	const AActor* Owner = GetOwner();
	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents(Primitives);
	TArray<FString> Unsupported;
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		for (int32 Index = 0; Index < Primitive->GetNumMaterials(); ++Index)
		{
			const UMaterialInterface* Material = Primitive->GetMaterial(Index);
			const FString Why = WhyMaterialIgnoresPortalClip(Material);
			if (!Why.IsEmpty())
			{
				Unsupported.AddUnique(FString::Printf(TEXT("%s (%s)"), Material ? *Material->GetPathName() : TEXT("(none)"), *Why));
			}
		}
	}
	if (Unsupported.Num() > 0)
	{
		UE_LOG(LogPortalWorlds, Warning, TEXT("%s has Clip At Portal on, but some of its materials don't support the cut, so it ")
			TEXT("won't be cut at the portal and will poke out of the far side: %s. Use a Masked material whose Opacity Mask comes ")
			TEXT("from /PortalWorlds/Materials/MF_PortalClip, or turn Clip At Portal off."),
			*Owner->GetActorNameOrLabel(), *FString::Join(Unsupported, TEXT(", ")));
	}
#endif
}

void UPortalTraversalComponent::UpdateCrossingCopy()
{
	AActor* Owner = GetOwner();
	if (Cast<APawn>(Owner))
	{
		return; // the player is never seen from outside
	}
	UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>();
	const FVector4f NoClip(0.0f, 0.0f, 0.0f, 0.0f);

	// The nearest portal that the owner's bounds reach, and where it leads from the owner's side.
	APortal* Through = nullptr;
	APortal* Link = nullptr;
	const FVector Centre = Eye ? Eye->GetComponentLocation() : Owner->GetActorLocation();
	if ((bShowCopyWhileCrossing || bClipAtPortal) && Subsystem && Eye)
	{
		FVector Origin, Extent;
		Owner->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);
		double Nearest = Extent.Size() + (Origin - Centre).Size();
		for (const TWeakObjectPtr<APortal>& PortalPtr : Subsystem->GetPortals())
		{
			APortal* Portal = PortalPtr.Get();
			const double Distance = Portal ? Portal->DistanceTo(Centre) : TNumericLimits<double>::Max();
			if (Distance < Nearest)
			{
				Nearest = Distance;
				Through = Portal;
			}
		}
		Link = Through ? Through->GetLinkFor(Centre) : nullptr;
	}

	const bool bClip = bClipAtPortal && CVarClipObjects.GetValueOnGameThread();

	// The owner keeps the side of the portal its centre is on: the rest of it is already through.
	FVector KeepNormal = FVector::ZeroVector;
	if (Link)
	{
		KeepNormal = Through->GetNormal() * (Through->SignedDistance(Centre) >= 0.0 ? 1.0 : -1.0);
	}
#if WITH_EDITOR
	if (Link && bClip && !bCheckedClipMaterials)
	{
		bCheckedClipMaterials = true;
		WarnIfMaterialsIgnoreClip();
	}
#endif
	SetClipPlane(Owner, Link && bClip ? MakeClipPlane(KeepNormal, Through->GetActorLocation()) : NoClip, OwnerClipPlane);
	if (Subsystem)
	{
		Subsystem->SetActorClippedAtPortal(Owner, Link && bClip ? Through : nullptr, KeepNormal);
	}
	UpdatePawnCollision(Link && bClip ? Through : nullptr, KeepNormal);

	if (!Link || !bShowCopyWhileCrossing)
	{
		if (CrossingCopy && !CrossingCopy->IsHidden())
		{
			CrossingCopy->SetActorHiddenInGame(true);
			CrossingCopy->SetActorEnableCollision(false);
			if (Subsystem)
			{
				Subsystem->SetActorClippedAtPortal(CrossingCopy, nullptr, FVector::ZeroVector);
			}
		}
		return;
	}

	AActor* Copy = GetOrCreateCrossingCopy();
	if (!Copy)
	{
		return;
	}
	const FMatrix Warp = Through->GetWarpTo(Link);
	CopyWarp = Warp;
	const FMatrix CopyMatrix = Owner->GetActorTransform().ToMatrixWithScale() * Warp;
	Copy->SetActorTransform(FTransform(CopyMatrix));
	Copy->SetActorHiddenInGame(false);
	Copy->SetActorEnableCollision(true);
	const bool bCopyMirrored = CopyMatrix.Determinant() < 0.0;
	if (bCopyMirrored != bCrossingCopyMirrored)
	{
		bCrossingCopyMirrored = bCopyMirrored;
		RefreshMirroredRendering(Copy);
	}

	// The copy keeps only what's already through: the warped image of the far side of the
	// entry portal, which is one side of the exit portal.
	FVector4f CopyPlane = NoClip;
	FVector CopyKeepNormal = FVector::ZeroVector;
	if (bClip)
	{
		const FVector FarSide = Warp.TransformPosition(Through->GetActorLocation() - KeepNormal * 10.0);
		CopyKeepNormal = Link->GetNormal() * (Link->SignedDistance(FarSide) >= 0.0 ? 1.0 : -1.0);
		CopyPlane = MakeClipPlane(CopyKeepNormal, Link->GetActorLocation());
	}
	SetClipPlane(Copy, CopyPlane, CopyClipPlane);
	Subsystem->SetActorClippedAtPortal(Copy, bClip ? Link : nullptr, CopyKeepNormal);

	// The copy blocks pawns like the object does (not while it's held, say), except pawns on the
	// copy's own cut-away side, which would bump into its invisible part.
	const bool bPawnOnCopyCutSide = bClip && IsPawnOnCutSide(Copy, Link, CopyKeepNormal);
	SetCopyPawnResponse(bPawnOnCopyCutSide ? ECR_Ignore : GetOwnPawnResponse());

	// The copy belongs to the exit portal's space, so views of that space show it.
	const int32 LinkSpace = UPortalSubsystem::GetActorSpace(Link);
	if (UPortalSubsystem::GetActorSpace(Copy) != LinkSpace)
	{
		Subsystem->SetActorSpace(Copy, LinkSpace);
	}
}

AActor* UPortalTraversalComponent::GetVisibleCrossingCopy(FMatrix& OutWarp) const
{
	if (!CrossingCopy || CrossingCopy->IsHidden())
	{
		return nullptr;
	}
	OutWarp = CopyWarp;
	return CrossingCopy;
}

void UPortalTraversalComponent::StopIgnoringPawns()
{
	if (!bIgnoringPawns)
	{
		return;
	}
	bIgnoringPawns = false;
	if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
	{
		Body->SetCollisionResponseToChannel(ECC_Pawn, PawnResponseBeforeIgnore);
	}
}

bool UPortalTraversalComponent::IsAnyPawnOverlapping() const
{
	const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Body)
	{
		return false;
	}
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const UPrimitiveComponent* PawnBody = Cast<UPrimitiveComponent>(It->GetRootComponent());
		if (PawnBody && PawnBody->IsCollisionEnabled()
			&& Body->OverlapComponent(PawnBody->GetComponentLocation(), PawnBody->GetComponentQuat(), PawnBody->GetCollisionShape()))
		{
			return true;
		}
	}
	return false;
}

bool UPortalTraversalComponent::IsPawnOnCutSide(const AActor* Actor, const APortal* Portal, const FVector& KeepNormal) const
{
	if (!Actor || !Portal)
	{
		return false;
	}
	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents*/ true, Origin, Extent);
	const FVector PortalLocation = Portal->GetActorLocation();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const APawn* Pawn = *It;
		const FVector PawnLocation = Pawn->GetActorLocation();
		if (FVector::DotProduct(PawnLocation - PortalLocation, KeepNormal) >= 0.0)
		{
			continue; // on the actor's own side: it's visible there, so it should block
		}
		float Radius, HalfHeight;
		Pawn->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const double Reach = Extent.Size() * 2.0 + FVector2D(Radius, HalfHeight).Size();
		if (Portal->DistanceTo(PawnLocation) < Reach)
		{
			return true;
		}
	}
	return false;
}

ECollisionResponse UPortalTraversalComponent::GetOwnPawnResponse() const
{
	const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Body || !Body->IsCollisionEnabled())
	{
		return ECR_Ignore;
	}
	return bIgnoringPawns ? PawnResponseBeforeIgnore.GetValue() : Body->GetCollisionResponseToChannel(ECC_Pawn);
}

void UPortalTraversalComponent::SetCopyPawnResponse(ECollisionResponse Response)
{
	if (!CrossingCopy || CopyPawnResponse == Response)
	{
		return;
	}
	CopyPawnResponse = Response;
	TArray<UPrimitiveComponent*> Primitives;
	CrossingCopy->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetCollisionResponseToChannel(ECC_Pawn, Response);
	}
}

void UPortalTraversalComponent::OnCopyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// A character walking into the copy pushes the real object, the way UCharacterMovementComponent
	// pushes a simulating body it bumps into (ApplyImpactPhysicsForces), carried back through the portal.
	const APawn* Pawn = Cast<APawn>(OtherActor);
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Pawn || !Body || !Body->IsSimulatingPhysics() || !CrossingCopy || CrossingCopy->IsHidden())
	{
		return;
	}
	if (Hit.bStartPenetrating)
	{
		return; // the pawn started inside the copy (it arrived there, or the copy moved into it): not a push
	}
	const UCharacterMovementComponent* Movement = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	if (!Movement || !Movement->bEnablePhysicsInteraction)
	{
		return;
	}

	// The hit can arrive from either side's point of view, so orient the normal towards the pawn.
	FVector Normal = Hit.ImpactNormal;
	if (FVector::DotProduct(Normal, Pawn->GetActorLocation() - Hit.ImpactPoint) < 0.0)
	{
		Normal = -Normal;
	}
	const FMatrix CopyToObject = CopyWarp.Inverse();
	const FVector Direction = CopyToObject.TransformVector(-Normal).GetSafeNormal();
	const FVector Point = CopyToObject.TransformPosition(Hit.ImpactPoint);

	const double Mass = Movement->bPushForceScaledToMass ? FMath::Max(Body->GetMass(), 1.0f) : 1.0;
	if (Body->GetPhysicsLinearVelocity().IsNearlyZero(1.0))
	{
		Body->AddImpulseAtLocation(Direction * (Mass * Movement->InitialPushForceFactor), Point);
	}
	else
	{
		Body->AddForceAtLocation(Direction * (Mass * Movement->PushForceFactor), Point);
	}
}

void UPortalTraversalComponent::UpdatePawnCollision(const APortal* Through, const FVector& KeepNormal)
{
	// The part of the object past the portal is cut away (it's drawn at the linked portal instead),
	// but its collision is still there. A pawn walking up to that side of the portal would bump into
	// it, and push the object, before reaching the portal. So while any pawn is on the cut-away side
	// near the portal, the object ignores pawns. (The pawn channel response covers both the
	// character's movement sweeps and the physics contact with its capsule.)
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Body)
	{
		return;
	}

	const bool bPawnOnCutSide = IsPawnOnCutSide(GetOwner(), Through, KeepNormal);
	if (bPawnOnCutSide && !bIgnoringPawns)
	{
		const ECollisionResponse Current = Body->GetCollisionResponseToChannel(ECC_Pawn);
		if (Current != ECR_Ignore) // already ignoring (e.g. held by UPortalGrabComponent): leave it alone
		{
			PawnResponseBeforeIgnore = Current;
			Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			bIgnoringPawns = true;
		}
	}
	else if (!bPawnOnCutSide && bIgnoringPawns && !IsAnyPawnOverlapping())
	{
		// Wait until no pawn is inside the object, or turning collision back on would shove it away.
		StopIgnoringPawns();
	}
}
