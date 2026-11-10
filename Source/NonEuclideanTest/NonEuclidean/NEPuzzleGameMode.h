#pragma once

#include "CoreMinimal.h"
#include "NEGameMode.h"
#include "NEPuzzleGameMode.generated.h"

class ANEPuzzleObject;
class ANEPuzzleSocket;
class APortal;

/**
 * Game mode for L_NE_ObjectPuzzles: the NE player and screenshot options, plus the puzzle HUD.
 *
 * Extra command-line options:
 *   -NEPuzzleTest   play through every puzzle by script (throwing and carrying objects through the
 *                   shrinking and mirroring portals, and filling the sockets), log PASS/FAIL for each
 *                   check ("[NE] PuzzleTest"), then quit.
 *   -NEPlace=<label>:X,Y,Z,Yaw,Size;...
 *                   freeze objects at these places before screenshots (a negative size mirrors).
 *   -NEPlaceLater=<same format>
 *                   the same, 1 s after play starts, once the objects have been rendered (e.g. to
 *                   check that mirroring an object that's already on screen renders it right side out).
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEPuzzleGameMode : public ANEGameMode
{
	GENERATED_BODY()

public:
	ANEPuzzleGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** A test step: called every frame with the seconds since it started; returns true when done. */
	using FStep = TFunction<bool(float Elapsed)>;

	void PlaceObjects(const FString& Places);
	FTimerHandle PlaceLaterTimer;
	void BuildSelfTest();

	// Step builders.
	void Do(TFunction<void()> Action);
	void WaitSeconds(float Seconds);
	void WaitUntil(const FString& What, TFunction<bool()> Condition, float Timeout);
	void Expect(const FString& What, TFunction<bool()> Condition, TFunction<FString()> Detail);
	void ThrowThrough(const FString& Object, const FString& Portal, float Speed);
	void PutInSocket(const FString& Object, const FString& Socket, bool bExpectSolved);
	void CarryThrough(const FString& Object, const FString& Portal);
	void WalkForwardThroughPortal(const FString& What);

	void Check(bool bPassed, const FString& What, const FString& Detail);

	ANEPuzzleObject* FindObject(const FString& Label) const;
	ANEPuzzleSocket* FindSocket(const FString& Label) const;
	APortal* FindPortal(const FString& Label) const;

	TArray<FStep> Steps;
	int32 StepIndex = 0;
	double StepStartTime = -1.0;
	int32 Passed = 0;
	int32 Failed = 0;
	bool bSelfTest = false;
	int32 MarkTraversals = 0;
};
