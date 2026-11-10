#include "PortalGrabComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Portal.h"
#include "PortalSubsystem.h"
#include "PortalTraversalComponent.h"
#include "PortalWorlds.h"

namespace
{
	/** How fast the allowance for pulling a just-grabbed object in shrinks (cm/s at the player's size). */
	constexpr double PullInSpeed = 300.0;

	/** An object out of reach for this long (s) is dropped; a quick turn only takes it out of reach briefly. */
	constexpr float StuckTimeLimit = 0.5f;

	/** Uniform scale of a warp or frame matrix. */
	double MatrixScale(const FMatrix& M)
	{
		return M.TransformVector(FVector::XAxisVector).Size();
	}
}

UPortalGrabComponent::UPortalGrabComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // after traversals; the handle applies the target before the next physics step
}

void UPortalGrabComponent::BeginPlay()
{
	Super::BeginPlay();

	Handle = NewObject<UPhysicsHandleComponent>(GetOwner(), TEXT("PortalGrabHandle"));
	Handle->bInterpolateTarget = false; // objects jump through portals; the target must jump with them
	// Stiffer than the defaults (750 / 200), so a carried object keeps up with a walking player
	// (the default spring trails ~0.8 m behind at walking speed) and reaches a portal before them.
	Handle->LinearStiffness = 4000.0f;
	Handle->LinearDamping = 200.0f;
	Handle->AngularStiffness = 4000.0f;
	Handle->AngularDamping = 400.0f;
	Handle->RegisterComponent();

	OwnerTraversal = GetOwner()->FindComponentByClass<UPortalTraversalComponent>();
	if (OwnerTraversal)
	{
		OwnerTraversal->OnPortalTraversed.AddDynamic(this, &UPortalGrabComponent::OnOwnerTraversed);
		AddTickPrerequisiteComponent(OwnerTraversal);
	}
}

void UPortalGrabComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Release();
	if (OwnerTraversal)
	{
		OwnerTraversal->OnPortalTraversed.RemoveDynamic(this, &UPortalGrabComponent::OnOwnerTraversed);
	}
	Super::EndPlay(Reason);
}

FVector UPortalGrabComponent::GetEyeLocation() const
{
	if (OwnerTraversal && OwnerTraversal->Eye)
	{
		return OwnerTraversal->Eye->GetComponentLocation();
	}
	if (const UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>())
	{
		return Camera->GetComponentLocation();
	}
	FVector Location;
	FRotator Rotation;
	GetOwner()->GetActorEyesViewPoint(Location, Rotation);
	return Location;
}

FVector UPortalGrabComponent::GetViewForward() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Pawn->GetViewRotation().Vector();
	}
	return GetOwner()->GetActorForwardVector();
}

double UPortalGrabComponent::GetPlayerScale() const
{
	return FMath::Abs(GetOwner()->GetActorScale3D().Z);
}

FMatrix UPortalGrabComponent::GetPlayerFrame() const
{
	// While mirrored, the player's true frame is the camera's frame reflected left-right: the
	// camera itself can't be mirrored, so the plugin flips the image instead (see UPortalSubsystem).
	const double S = GetPlayerScale();
	const bool bMirrored = OwnerTraversal && OwnerTraversal->IsMirrored();
	const FRotator Heading(0.0, GetViewForward().Rotation().Yaw, 0.0);
	return FScaleMatrix(FVector(S, bMirrored ? -S : S, S)) * FRotationTranslationMatrix(Heading, GetEyeLocation());
}

UPrimitiveComponent* UPortalGrabComponent::TraceForObject(FMatrix& OutWarp) const
{
	OutWarp = FMatrix::Identity;
	const UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>();

	FVector From = GetEyeLocation();
	FVector To = From + GetViewForward() * (GrabRange * GetPlayerScale());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PortalGrab), /*bTraceComplex*/ false, GetOwner());
	const APortal* Skip = nullptr;

	for (int32 Hop = 0; Hop < 3; ++Hop)
	{
		// The first portal along the segment.
		const APortal* Through = nullptr;
		double Fraction = 1.0;
		if (Subsystem)
		{
			for (const TWeakObjectPtr<APortal>& PortalPtr : Subsystem->GetPortals())
			{
				const APortal* Portal = PortalPtr.Get();
				if (!Portal || Portal == Skip || !Portal->Intersects(From, To, FVector::ZeroVector))
				{
					continue;
				}
				const double DA = Portal->SignedDistance(From);
				const double DB = Portal->SignedDistance(To);
				const double T = DA / (DA - DB);
				if (T < Fraction)
				{
					Fraction = T;
					Through = Portal;
				}
			}
		}

		const FVector End = From + (To - From) * Fraction;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, End, ECC_Visibility, Params))
		{
			UPrimitiveComponent* Component = Hit.GetComponent();
			if (!Component || !Component->IsSimulatingPhysics())
			{
				return nullptr;
			}
			// Size limit, measured in the player's frame.
			const double RelativeRadius = Component->Bounds.SphereRadius / MatrixScale(OutWarp);
			return RelativeRadius <= MaxObjectRadius * GetPlayerScale() ? Component : nullptr;
		}
		const APortal* Link = Through ? Through->GetLinkFor(From) : nullptr;
		if (!Link)
		{
			return nullptr;
		}
		const FMatrix Warp = Through->GetWarpTo(Link);
		From = Warp.TransformPosition(End);
		To = Warp.TransformPosition(To);
		OutWarp = OutWarp * Warp;
		Skip = Link; // the trace now starts on its surface
	}
	return nullptr;
}

UPrimitiveComponent* UPortalGrabComponent::GetAimedComponent() const
{
	FMatrix Warp;
	return TraceForObject(Warp);
}

bool UPortalGrabComponent::Grab()
{
	if (IsHolding())
	{
		return false;
	}
	FMatrix Warp;
	UPrimitiveComponent* Component = TraceForObject(Warp);
	if (!Component)
	{
		return false;
	}

	// The object as it would be on the player's side of the portals, relative to the player.
	const FMatrix Frame = GetPlayerFrame();
	const FMatrix ObjectHere = Component->GetComponentTransform().ToMatrixWithScale() * Warp.Inverse();
	HeldInFrame = ObjectHere * Frame.Inverse();
	HeldInFrame.SetOrigin(FVector::ZeroVector);
	HeldWarp = Warp;
	const double S = GetPlayerScale();
	const double Radius = Component->Bounds.SphereRadius / MatrixScale(Warp);
	HeldDistance = (HoldGap * S + Radius) / S;

	HeldComponent = Component;
	StuckTime = 0.0f;
	AllowedDistance = FVector::Dist(Component->GetComponentLocation(), GetHoldTarget().GetLocation()) / (S * MatrixScale(Warp)) + BreakDistance;
	HeldPawnResponse = Component->GetCollisionResponseToChannel(ECC_Pawn);
	Component->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Component->WakeRigidBody();
	Handle->GrabComponentAtLocationWithRotation(Component, NAME_None, Component->GetComponentLocation(), Component->GetComponentRotation());

	HeldTraversal = Component->GetOwner()->FindComponentByClass<UPortalTraversalComponent>();
	if (UPortalTraversalComponent* Traversal = HeldTraversal.Get())
	{
		Traversal->OnPortalTraversed.AddDynamic(this, &UPortalGrabComponent::OnHeldTraversed);
		AddTickPrerequisiteComponent(Traversal);
	}
	UE_LOG(LogPortalWorlds, Log, TEXT("Grabbed %s (relative size %.3f)"), *Component->GetOwner()->GetActorNameOrLabel(), GetHeldRelativeScale());
	return true;
}

void UPortalGrabComponent::Release()
{
	UPrimitiveComponent* Component = HeldComponent.Get();
	if (Handle)
	{
		Handle->ReleaseComponent();
	}
	if (UPortalTraversalComponent* Traversal = HeldTraversal.Get())
	{
		Traversal->OnPortalTraversed.RemoveDynamic(this, &UPortalGrabComponent::OnHeldTraversed);
		RemoveTickPrerequisiteComponent(Traversal);
	}
	if (Component)
	{
		Component->SetCollisionResponseToChannel(ECC_Pawn, HeldPawnResponse);
		UE_LOG(LogPortalWorlds, Log, TEXT("Released %s"), *Component->GetOwner()->GetActorNameOrLabel());
	}
	HeldComponent = nullptr;
	HeldTraversal = nullptr;
	HeldWarp = FMatrix::Identity;
}

void UPortalGrabComponent::Throw()
{
	UPrimitiveComponent* Component = HeldComponent.Get();
	if (!Component)
	{
		return;
	}
	// Forwards for the player, carried to the object's side of any portals between them.
	const FVector Velocity = HeldWarp.TransformVector(GetViewForward() * (ThrowSpeed * GetPlayerScale()));
	Release();
	Component->SetPhysicsLinearVelocity(Velocity);
}

void UPortalGrabComponent::ToggleGrab()
{
	if (IsHolding())
	{
		Release();
	}
	else
	{
		Grab();
	}
}

float UPortalGrabComponent::GetHeldRelativeScale() const
{
	const UPrimitiveComponent* Component = HeldComponent.Get();
	if (!Component)
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::Abs(Component->GetComponentScale().Z) / MatrixScale(HeldWarp) / GetPlayerScale());
}

FTransform UPortalGrabComponent::GetHoldTarget() const
{
	FMatrix Target = HeldInFrame * GetPlayerFrame();
	Target.SetOrigin(GetEyeLocation() + GetViewForward() * (HeldDistance * GetPlayerScale()));
	return FTransform(Target * HeldWarp);
}

void UPortalGrabComponent::OnOwnerTraversed(APortal* FromPortal, APortal* ToPortal, float ScaleFactor)
{
	// The player moved by Warp but the object didn't: it's now on the other side of that portal.
	if (IsHolding() && FromPortal && ToPortal)
	{
		HeldWarp = ToPortal->GetWarpTo(FromPortal) * HeldWarp;
	}
}

void UPortalGrabComponent::OnHeldTraversed(APortal* FromPortal, APortal* ToPortal, float ScaleFactor)
{
	UPrimitiveComponent* Component = HeldComponent.Get();
	if (Component && FromPortal && ToPortal)
	{
		HeldWarp = HeldWarp * FromPortal->GetWarpTo(ToPortal);
		// Grab it afresh where it is now. The handle moves its kinematic anchor to a new target
		// over a physics step; across a portal that's a jump of metres, and the anchor's speed
		// would fling the object. A new grab creates the anchor in place.
		Handle->ReleaseComponent();
		Handle->GrabComponentAtLocationWithRotation(Component, NAME_None, Component->GetComponentLocation(), Component->GetComponentRotation());
	}
}

void UPortalGrabComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UPrimitiveComponent* Component = HeldComponent.Get();
	if (!IsHolding())
	{
		return;
	}
	if (!Component->IsSimulatingPhysics())
	{
		Release(); // something else took over the object (e.g. it was locked into place)
		return;
	}

	// Snap the warp back to exactly identity once the player and the object are on the same side
	// again, so rounding doesn't build up over many trips.
	if (HeldWarp.Equals(FMatrix::Identity, 1e-6))
	{
		HeldWarp = FMatrix::Identity;
	}

	// Distances in the player's units, so they mean the same at any size and through any portal.
	const FTransform Target = GetHoldTarget();
	const double Distance = FVector::Dist(Component->GetComponentLocation(), Target.GetLocation()) / (GetPlayerScale() * MatrixScale(HeldWarp));
	AllowedDistance = FMath::Max<double>(BreakDistance, AllowedDistance - DeltaTime * PullInSpeed);
	StuckTime = Distance > AllowedDistance ? StuckTime + DeltaTime : 0.0f;
	if (StuckTime > StuckTimeLimit)
	{
		UE_LOG(LogPortalWorlds, Log, TEXT("Dropped %s: it got stuck"), *Component->GetOwner()->GetActorNameOrLabel());
		Release();
		return;
	}
	Handle->SetTargetLocationAndRotation(Target.GetLocation(), Target.GetRotation().Rotator());
}
