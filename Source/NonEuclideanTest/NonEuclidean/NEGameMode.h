#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NEGameMode.generated.h"

/**
 * Game mode for the non-Euclidean test maps: spawns ANEPlayerCharacter and reuses the
 * template's player controller (input mapping contexts + ANonEuclideanTestCameraManager).
 *
 * Automated comparison shots (see Tools/NonEuclidean/shot.sh):
 *   -NEScreenshot[=<settle seconds>]  wait for shader compilation, settle, save
 *                                     Saved/Screenshots/<platform>/NE_<map>.png and quit.
 *   -NEPose=X,Y,Z,Yaw,Pitch           take the shot from this eye position (cm) and rotation
 *                                     (degrees), e.g. one fitted to a video frame. Freezes movement.
 *   -NEScale=<s>                      start the player at this size (1 = normal), as if shrunk by portals.
 *   -NEPoses=<pose>|<pose>|...       one shot per pose (NE_<map>_pose<i>.png), all in one run.
 *   -NESpin=<deg/s> -NEStrafe=<cm/s> -NEForward=<cm/s>
 *                                     after settling, turn/strafe/walk for -NEMotionFrames (default 30)
 *                                     frames, then shoot. Motion advances by a fixed 1/60 s per frame
 *                                     (scaled by the pawn's size) and is applied incrementally, so it
 *                                     carries on through portals and the final pose is identical
 *                                     between runs.
 *   -NEScript=<frames>:<cm/s>:<deg/s>;...
 *                                     a path: each segment walks forward and turns at these rates
 *                                     for that many frames (replaces -NEForward/-NESpin).
 *   -NEProfile                        during the motion, record frame / game thread / render thread /
 *                                     GPU times and portal captures, and log mean, p95 and max.
 *   -NESequence                       also save every motion frame as NE_<map>_<frame>.png
 *                                     (-NESequenceEvery=<k>: only every k-th frame).
 *   -NEUseInput                       walk with real movement input (character movement, collision,
 *                                     real frame time) instead of moving the pawn directly; the
 *                                     forward/strafe values only give the direction.
 */
UCLASS()
class NONEUCLIDEANTEST_API ANEGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANEGameMode();

	/** Demo maps in the original's order (its keys 1-6; its 7 was the out-of-scope VR level), then the mirror tunnel (7, new) and the object puzzles (8, new). */
	static const TArray<FString>& GetDemoMaps();

	/** Opens demo Index (0-based). Bound to keys 1-8 and the console command "NE.Demo <1-8>". */
	static void OpenDemo(UObject* WorldContext, int32 Index);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void PollScreenshot();
	void ApplyPoseFromCommandLine();
	bool ApplyPose(const FString& PoseString);
	void TakeScreenshot(const FString& Suffix, bool bFinal);

	FTimerHandle ScreenshotTimer;
	bool bPoseApplied = false;
	float ScreenshotSettleSeconds = 2.0f;
	double ScreenshotReadyTime = -1.0;
	bool bScreenshotTaken = false;

	// Deterministic motion before the shot.
	float SpinDegreesPerSecond = 0.0f;
	float StrafeCmPerSecond = 0.0f;
	float ForwardCmPerSecond = 0.0f;
	int32 MotionFrameCount = 30;
	bool bSequence = false;
	int32 SequenceEvery = 1;
	bool bProfile = false;
	double LastFrameTime = 0.0;
	TArray<float> ProfileFrameMs, ProfileGameMs, ProfileRenderMs, ProfileGpuMs, ProfileCaptures;
	void LogProfile() const;

	TArray<FString> PoseList;
	int32 PoseListIndex = INDEX_NONE;
	struct FScriptSegment { int32 Frames; float Forward; float Spin; };
	TArray<FScriptSegment> Script;
	bool bUseInput = false;
	bool bMotionActive = false;
	int32 MotionFrames = 0;
	FRotator MotionBaseRotation = FRotator::ZeroRotator;
	FVector MotionBaseLocation = FVector::ZeroVector;
};
