#include "NEPuzzleObject.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PortalTraversalComponent.h"

ANEPuzzleObject::ANEPuzzleObject()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);
	Mesh->SetGenerateOverlapEvents(true);
	Mesh->SetCastShadow(false); // the maps are unlit
	Mesh->BodyInstance.bUseCCD = true; // small objects are thrown at walls and through portals

	PortalTraversal = CreateDefaultSubobject<UPortalTraversalComponent>(TEXT("PortalTraversal"));
}

void ANEPuzzleObject::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (ObjectMesh)
	{
		Mesh->SetStaticMesh(ObjectMesh);
	}
}

float ANEPuzzleObject::GetSize() const
{
	return static_cast<float>(FMath::Abs(GetActorScale3D().Z));
}

bool ANEPuzzleObject::IsMirrored() const
{
	const FVector Scale = GetActorScale3D();
	return Scale.X * Scale.Y * Scale.Z < 0.0;
}

void ANEPuzzleObject::LockInto(const FTransform& Transform)
{
	bLocked = true;
	PlaceAt(Transform, /*bFreeze*/ true);
}

void ANEPuzzleObject::PlaceAt(const FTransform& Transform, bool bFreeze)
{
	if (bFreeze)
	{
		Mesh->SetSimulatePhysics(false);
	}
	const bool bWasMirrored = IsMirrored();
	SetActorTransform(Transform, false, nullptr, ETeleportType::ResetPhysics);
	if (IsMirrored() != bWasMirrored)
	{
		UPortalTraversalComponent::RefreshMirroredRendering(this); // or it would render inside out
	}
	if (Mesh->IsSimulatingPhysics())
	{
		Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Mesh->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
	PortalTraversal->ResetTracking(); // a jump, not a walk through a portal
}

FString ANEPuzzleObject::DescribeState() const
{
	return FString::Printf(TEXT("size x%.2f%s"), GetSize(), IsMirrored() ? TEXT(", mirrored") : TEXT(""));
}
