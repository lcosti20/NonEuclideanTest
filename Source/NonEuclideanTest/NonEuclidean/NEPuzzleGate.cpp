#include "NEPuzzleGate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "NEPuzzleSocket.h"
#include "UObject/ConstructorHelpers.h"

ANEPuzzleGate::ANEPuzzleGate()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCastShadow(false);
}

void ANEPuzzleGate::BeginPlay()
{
	Super::BeginPlay();
	ClosedLocation = GetActorLocation();
}

void ANEPuzzleGate::CountSolved(int32& OutSolved, int32& OutTotal) const
{
	OutSolved = 0;
	OutTotal = 0;
	for (const ANEPuzzleSocket* Socket : Sockets)
	{
		if (Socket)
		{
			++OutTotal;
			OutSolved += Socket->IsSolved() ? 1 : 0;
		}
	}
}

void ANEPuzzleGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bOpen)
	{
		int32 Solved, Total;
		CountSolved(Solved, Total);
		if (Total == 0 || Solved < Total)
		{
			return;
		}
		bOpen = true;
		UE_LOG(LogTemp, Display, TEXT("[NE] gate %s opening: all %d sockets solved"), *GetActorNameOrLabel(), Total);
	}

	if (OpenAlpha < 1.0f)
	{
		OpenAlpha = FMath::Min(1.0f, OpenAlpha + DeltaSeconds / FMath::Max(OpenTime, 0.01f));
		const double Height = Mesh->Bounds.BoxExtent.Z * 2.0 + 2.0;
		SetActorLocation(ClosedLocation - FVector(0.0, 0.0, Height * FMath::SmoothStep(0.0f, 1.0f, OpenAlpha)));
		if (OpenAlpha >= 1.0f)
		{
			SetActorEnableCollision(false);
			SetActorHiddenInGame(true);
		}
	}
}
