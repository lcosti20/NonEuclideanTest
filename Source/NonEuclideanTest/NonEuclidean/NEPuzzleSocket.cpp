#include "NEPuzzleSocket.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NEPuzzleObject.h"
#include "PortalGrabComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float PedestalHeight = 40.0f; // cm
	constexpr float PedestalWidth = 80.0f;
	const FLinearColor LampIdle(0.7f, 0.7f, 0.75f);
	const FLinearColor LampWrong(0.95f, 0.15f, 0.1f);
	const FLinearColor LampSolved(0.15f, 0.95f, 0.3f);
	const FName ColorParam(TEXT("Color"));
}

ANEPuzzleSocket::ANEPuzzleSocket()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Pedestal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal"));
	Pedestal->SetupAttachment(Root);
	Pedestal->SetStaticMesh(CubeFinder.Object);
	Pedestal->SetRelativeLocation(FVector(0.0f, 0.0f, PedestalHeight * 0.5f));
	Pedestal->SetRelativeScale3D(FVector(PedestalWidth, PedestalWidth, PedestalHeight) / 100.0f);
	Pedestal->SetCastShadow(false);

	Lamp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Root);
	Lamp->SetStaticMesh(SphereFinder.Object);
	Lamp->SetRelativeLocation(FVector(PedestalWidth * 0.5f, 0.0f, PedestalHeight));
	Lamp->SetRelativeScale3D(FVector(0.16f));
	Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lamp->SetCastShadow(false);

	Ghost = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ghost"));
	Ghost->SetupAttachment(Root);
	Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ghost->SetCastShadow(false);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
}

FMatrix ANEPuzzleSocket::GetGhostRelativeMatrix() const
{
	const FBox Box = GhostMesh ? GhostMesh->GetBoundingBox() : FBox(FVector(-50.0), FVector(50.0));
	const double S = RequiredSize;
	const double Lift = PedestalHeight - Box.Min.Z * S + 0.5; // rests on the pedestal
	return FScaleMatrix(FVector(bRequireMirrored ? -S : S, S, S))
		* FRotationTranslationMatrix(FRotator(0.0, GhostYaw, 0.0), FVector(0.0, 0.0, Lift));
}

FTransform ANEPuzzleSocket::GetGhostTransform() const
{
	// As matrices: a mirrored child of a rotated parent doesn't compose reliably as FTransforms.
	return FTransform(GetGhostRelativeMatrix() * GetActorTransform().ToMatrixWithScale());
}

void ANEPuzzleSocket::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	Ghost->SetStaticMesh(GhostMesh);
	Ghost->SetRelativeTransform(FTransform(GetGhostRelativeMatrix()));
	if (GhostMaterial && GhostMesh)
	{
		for (int32 Index = 0; Index < Ghost->GetNumMaterials(); ++Index)
		{
			Ghost->SetMaterial(Index, GhostMaterial);
		}
	}
	if (PedestalMaterial)
	{
		Pedestal->SetMaterial(0, PedestalMaterial);
	}
	if (LampMaterial)
	{
		Lamp->SetMaterial(0, LampMaterial);
	}

	// The trigger surrounds the ghost generously, so an object dropped roughly on top is found.
	const FBox Box = GhostMesh ? GhostMesh->GetBoundingBox() : FBox(FVector(-50.0), FVector(50.0));
	const double Extent = Box.GetExtent().GetMax() * RequiredSize * 1.2 + 10.0;
	Trigger->SetRelativeLocation(FVector(0.0, 0.0, PedestalHeight + Extent * 0.9));
	Trigger->SetBoxExtent(FVector(FMath::Max(Extent, PedestalWidth * 0.5), FMath::Max(Extent, PedestalWidth * 0.5), Extent));
}

void ANEPuzzleSocket::BeginPlay()
{
	Super::BeginPlay();
	LampInstance = Lamp->CreateDynamicMaterialInstance(0);
	SetLamp(LampIdle);
}

void ANEPuzzleSocket::SetLamp(const FLinearColor& Color)
{
	if (LampInstance && Color != LampColor)
	{
		LampInstance->SetVectorParameterValue(ColorParam, Color);
		LampColor = Color;
	}
}

FString ANEPuzzleSocket::DescribeRequirement() const
{
	return FString::Printf(TEXT("%s%s at size x%.2f"), bRequireMirrored ? TEXT("mirrored ") : TEXT(""),
		*RequiredKind.ToString().ToLower(), RequiredSize);
}

bool ANEPuzzleSocket::IsHeld(const ANEPuzzleObject* Object) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UPortalGrabComponent* Grab = Pawn ? Pawn->FindComponentByClass<UPortalGrabComponent>() : nullptr;
	const UPrimitiveComponent* Held = Grab ? Grab->GetHeldComponent() : nullptr;
	return Held && Held->GetOwner() == Object;
}

void ANEPuzzleSocket::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bSolved)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, ANEPuzzleObject::StaticClass());

	// Report the most promising object: the right kind first.
	ANEPuzzleObject* Candidate = nullptr;
	for (AActor* Actor : Overlapping)
	{
		ANEPuzzleObject* Object = Cast<ANEPuzzleObject>(Actor);
		if (Object && !Object->IsLocked() && (!Candidate || Object->Kind == RequiredKind))
		{
			Candidate = Object;
		}
	}

	if (!Candidate)
	{
		Status = TEXT("empty");
		SetLamp(LampIdle);
		return;
	}

	const float Size = Candidate->GetSize();
	if (Candidate->Kind != RequiredKind)
	{
		Status = FString::Printf(TEXT("wrong object (%s)"), *Candidate->Label);
	}
	else if (FMath::Abs(Size - RequiredSize) > SizeTolerance * RequiredSize)
	{
		Status = FString::Printf(TEXT("%s is the wrong size (x%.2f)"), *Candidate->Label, Size);
	}
	else if (Candidate->IsMirrored() != bRequireMirrored)
	{
		Status = FString::Printf(TEXT("%s is the wrong way round (%s)"), *Candidate->Label,
			Candidate->IsMirrored() ? TEXT("mirrored") : TEXT("not mirrored"));
	}
	else if (IsHeld(Candidate))
	{
		Status = FString::Printf(TEXT("%s fits: let go of it"), *Candidate->Label);
		SetLamp(LampIdle);
		return;
	}
	else
	{
		// The handedness matches, so the ghost's transform (same sign of scale) puts the object
		// exactly into the ghost's shape.
		Candidate->LockInto(GetGhostTransform());
		bSolved = true;
		Status = FString::Printf(TEXT("solved with %s"), *Candidate->Label);
		Ghost->SetVisibility(false);
		SetLamp(LampSolved);
		UE_LOG(LogTemp, Display, TEXT("[NE] socket %s solved by %s (%s)"), *Label, *Candidate->Label, *Candidate->DescribeState());
		return;
	}
	SetLamp(LampWrong);
}
