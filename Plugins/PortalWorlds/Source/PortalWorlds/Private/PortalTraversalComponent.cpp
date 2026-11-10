#include "PortalTraversalComponent.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
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
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(Source->CastShadow);
		Mesh->SetupAttachment(Root);
		Mesh->SetRelativeTransform(FTransform(Source->GetComponentTransform().ToMatrixWithScale() * ToOwnerLocal));
		Mesh->RegisterComponent();
		CrossingCopy->AddInstanceComponent(Mesh);
	}
	CrossingCopy->SetActorHiddenInGame(true);
	bCrossingCopyMirrored = Owner->GetActorTransform().ToMatrixWithScale().Determinant() < 0.0;
	return CrossingCopy;
}

void UPortalTraversalComponent::UpdateCrossingCopy()
{
	AActor* Owner = GetOwner();
	if (Cast<APawn>(Owner))
	{
		return; // the player is never seen from outside
	}
	UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>();

	// The nearest portal that the owner's bounds reach, and where it leads from the owner's side.
	APortal* Through = nullptr;
	APortal* Link = nullptr;
	if (bShowCopyWhileCrossing && Subsystem && Eye)
	{
		const FVector Centre = Eye->GetComponentLocation();
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

	if (!Link)
	{
		if (CrossingCopy && !CrossingCopy->IsHidden())
		{
			CrossingCopy->SetActorHiddenInGame(true);
		}
		return;
	}

	AActor* Copy = GetOrCreateCrossingCopy();
	if (!Copy)
	{
		return;
	}
	const FMatrix CopyMatrix = Owner->GetActorTransform().ToMatrixWithScale() * Through->GetWarpTo(Link);
	Copy->SetActorTransform(FTransform(CopyMatrix));
	Copy->SetActorHiddenInGame(false);
	const bool bCopyMirrored = CopyMatrix.Determinant() < 0.0;
	if (bCopyMirrored != bCrossingCopyMirrored)
	{
		bCrossingCopyMirrored = bCopyMirrored;
		RefreshMirroredRendering(Copy);
	}

	// The copy belongs to the exit portal's space, so views of that space show it.
	const int32 LinkSpace = UPortalSubsystem::GetActorSpace(Link);
	if (UPortalSubsystem::GetActorSpace(Copy) != LinkSpace)
	{
		Subsystem->SetActorSpace(Copy, LinkSpace);
	}
}
