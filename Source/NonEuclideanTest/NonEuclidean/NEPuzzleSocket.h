#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NEPuzzleSocket.generated.h"

class ANEPuzzleObject;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A pedestal that wants one object of a given kind, size and handedness. A translucent "ghost" on
 * top shows exactly the shape it wants (at the required size, mirrored if a mirrored object is
 * required). Drop a matching object on it and it locks into the ghost's place; the lamp turns
 * green. A wrong object lights the lamp red, and the HUD says what's wrong.
 *
 * The pedestal's front (the lamp) faces the actor's +X.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEPuzzleSocket : public AActor
{
	GENERATED_BODY()

public:
	ANEPuzzleSocket();

	/** Name shown on the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	FString Label = TEXT("Socket");

	/** Kind of object accepted (ANEPuzzleObject::Kind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	FName RequiredKind = TEXT("Crate");

	/** Size accepted (ANEPuzzleObject::GetSize). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	float RequiredSize = 1.0f;

	/** Accept only mirrored objects (true) or only unmirrored ones (false). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	bool bRequireMirrored = false;

	/** Relative size tolerance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	float SizeTolerance = 0.15f;

	/** The accepted object's mesh, shown as the ghost. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	TObjectPtr<UStaticMesh> GhostMesh;

	/** Yaw of the ghost on the pedestal (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	float GhostYaw = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UMaterialInterface> PedestalMaterial;

	/** Lamp material; needs a vector parameter "Color". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<UMaterialInterface> LampMaterial;

	UFUNCTION(BlueprintPure, Category = "Puzzle")
	bool IsSolved() const { return bSolved; }

	/** "a crate at size x0.50" */
	FString DescribeRequirement() const;

	/** What's happening on the pedestal: "empty", "solved", or what's wrong with the object on it. */
	const FString& GetStatus() const { return Status; }

	/** Where an accepted object is locked (world space, including the mirroring). */
	FTransform GetGhostTransform() const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	FMatrix GetGhostRelativeMatrix() const;
	void SetLamp(const FLinearColor& Color);
	bool IsHeld(const ANEPuzzleObject* Object) const;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Pedestal;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Lamp;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Ghost;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LampInstance;

	bool bSolved = false;
	FString Status = TEXT("empty");
	FLinearColor LampColor = FLinearColor::Black;
};
