#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NEPuzzleGate.generated.h"

class ANEPuzzleSocket;
class UStaticMeshComponent;

/** A slab that sinks into the ground once all its sockets are solved. */
UCLASS()
class NONEUCLIDEANTEST_API ANEPuzzleGate : public AActor
{
	GENERATED_BODY()

public:
	ANEPuzzleGate();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Puzzle")
	TArray<TObjectPtr<ANEPuzzleSocket>> Sockets;

	/** Seconds to sink. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle")
	float OpenTime = 2.0f;

	UFUNCTION(BlueprintPure, Category = "Puzzle")
	bool IsOpen() const { return bOpen; }

	/** Sockets solved / total. */
	void CountSolved(int32& OutSolved, int32& OutTotal) const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector ClosedLocation = FVector::ZeroVector;
	bool bOpen = false;
	float OpenAlpha = 0.0f;
};
