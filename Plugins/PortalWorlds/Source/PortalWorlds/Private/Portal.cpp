#include "Portal.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PortalSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName PortalTexParam(TEXT("PortalTex"));
	const FName PinkParam(TEXT("Pink"));
	const FName MirrorUParam(TEXT("MirrorU"));
}

APortal::APortal()
{
	PrimaryActorTick.bCanEverTick = false;

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);
	Surface->SetCollisionProfileName(TEXT("NoCollision"));
	Surface->SetCastShadow(false);
	Surface->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> QuadFinder(TEXT("/PortalWorlds/Meshes/SM_PortalQuad"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/PortalWorlds/Materials/M_Portal"));
	if (QuadFinder.Succeeded())
	{
		Surface->SetStaticMesh(QuadFinder.Object);
	}
	if (MaterialFinder.Succeeded())
	{
		Surface->SetMaterial(0, MaterialFinder.Object);
	}
}

void APortal::Connect(APortal* A, APortal* B)
{
	if (A && B)
	{
		A->FrontLink = B;
		A->BackLink = B;
		B->FrontLink = A;
		B->BackLink = A;
	}
}

void APortal::BeginPlay()
{
	Super::BeginPlay();

	SurfaceMaterial = Surface->CreateDynamicMaterialInstance(0);
	ShowPink();

	if (UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>())
	{
		Subsystem->RegisterPortal(this);
	}
}

void APortal::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UPortalSubsystem* Subsystem = GetWorld()->GetSubsystem<UPortalSubsystem>())
	{
		Subsystem->UnregisterPortal(this);
	}
	Super::EndPlay(Reason);
}

FVector APortal::GetNormal() const
{
	return GetActorTransform().TransformVectorNoScale(FVector(0.0, -1.0, 0.0));
}

double APortal::SignedDistance(const FVector& Point) const
{
	return FVector::DotProduct(Point - GetActorLocation(), GetNormal());
}

APortal* APortal::GetLinkFor(const FVector& Point) const
{
	const double D = SignedDistance(Point);
	if (D > 0.0)
	{
		return FrontLink;
	}
	if (D < 0.0)
	{
		return BackLink;
	}
	return nullptr;
}

void APortal::GetCorners(FVector OutCorners[4]) const
{
	const FTransform& T = GetActorTransform();
	const double HX = SurfaceHalfExtent.X;
	const double HZ = SurfaceHalfExtent.Y;
	OutCorners[0] = T.TransformPosition(FVector(-HX, 0.0, -HZ));
	OutCorners[1] = T.TransformPosition(FVector(HX, 0.0, -HZ));
	OutCorners[2] = T.TransformPosition(FVector(HX, 0.0, HZ));
	OutCorners[3] = T.TransformPosition(FVector(-HX, 0.0, HZ));
}

double APortal::DistanceTo(const FVector& Point) const
{
	// Closest point on the rectangle.
	const FTransform& T = GetActorTransform();
	const FVector Center = T.GetLocation();
	const FVector X = T.TransformVector(FVector(SurfaceHalfExtent.X, 0.0, 0.0)); // half-extent axes
	const FVector Z = T.TransformVector(FVector(0.0, 0.0, SurfaceHalfExtent.Y));
	const FVector V = Point - Center;
	const double PX = FMath::Clamp(FVector::DotProduct(V, X) / X.SizeSquared(), -1.0, 1.0);
	const double PZ = FMath::Clamp(FVector::DotProduct(V, Z) / Z.SizeSquared(), -1.0, 1.0);
	return (V - (X * PX + Z * PZ)).Size();
}

bool APortal::Intersects(const FVector& From, const FVector& To, const FVector& Bump) const
{
	const FTransform& T = GetActorTransform();
	const FVector N = GetNormal();
	const FVector P = T.GetLocation() + Bump;
	const double DA = FVector::DotProduct(From - P, N);
	const double DB = FVector::DotProduct(To - P, N);
	if (DA * DB > 0.0 || DA == DB)
	{
		return false; // both ends on the same side
	}
	const FVector Hit = From + (To - From) * (DA / (DA - DB)) - P;
	const FVector X = T.TransformVector(FVector(SurfaceHalfExtent.X, 0.0, 0.0)); // half-extent axes
	const FVector Z = T.TransformVector(FVector(0.0, 0.0, SurfaceHalfExtent.Y));
	return FMath::Abs(FVector::DotProduct(Hit, X)) < X.SizeSquared()
		&& FMath::Abs(FVector::DotProduct(Hit, Z)) < Z.SizeSquared();
}

FMatrix APortal::GetWarpTo(const APortal* Target) const
{
	return GetActorTransform().ToInverseMatrixWithScale() * Target->GetActorTransform().ToMatrixWithScale();
}

bool APortal::IsMirroredLinkTo(const APortal* Target) const
{
	return Target && GetWarpTo(Target).Determinant() < 0.0;
}

void APortal::ShowTexture(UTexture* Texture, bool bMirrorU)
{
	if (!SurfaceMaterial)
	{
		return;
	}
	if (bShowingPink || ShownTexture != Texture)
	{
		SurfaceMaterial->SetTextureParameterValue(PortalTexParam, Texture);
		SurfaceMaterial->SetScalarParameterValue(PinkParam, 0.0f);
		ShownTexture = Texture;
		bShowingPink = false;
	}
	if (bShownMirrorU != bMirrorU)
	{
		SurfaceMaterial->SetScalarParameterValue(MirrorUParam, bMirrorU ? 1.0f : 0.0f);
		bShownMirrorU = bMirrorU;
	}
}

void APortal::ShowPink()
{
	if (SurfaceMaterial && !bShowingPink)
	{
		SurfaceMaterial->SetScalarParameterValue(PinkParam, 1.0f);
		bShowingPink = true;
	}
}
