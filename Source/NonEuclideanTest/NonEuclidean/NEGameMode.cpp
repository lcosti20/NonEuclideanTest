#include "NEGameMode.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Misc/CommandLine.h"
#include "NEPlayerCharacter.h"
#include "PortalSubsystem.h"
#include "PortalTraversalComponent.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double MotionStep = 1.0 / 60.0; // fixed step per frame, independent of real frame time
}

const TArray<FString>& ANEGameMode::GetDemoMaps()
{
	static const TArray<FString> Maps = {
		TEXT("/Game/NonEuclidean/Maps/L_NE_Tunnels"), // 1: long tunnel short inside, short tunnel long inside
		TEXT("/Game/NonEuclidean/Maps/L_NE_House3"),  // 2: four-room house with three rooms
		TEXT("/Game/NonEuclidean/Maps/L_NE_House6"),  // 3: four-room house with six rooms
		TEXT("/Game/NonEuclidean/Maps/L_NE_Pillar"),  // 4: walk around the pillar
		TEXT("/Game/NonEuclidean/Maps/L_NE_Slope"),   // 5: down the tunnel to the top of the hill
		TEXT("/Game/NonEuclidean/Maps/L_NE_Scale"),   // 6: shrinking tunnel
		TEXT("/Game/NonEuclidean/Maps/L_NE_Mirror"),  // 7: mirror tunnel (not in the original)
		TEXT("/Game/NonEuclidean/Maps/L_NE_ObjectPuzzles"), // 8: carrying objects through shrinking and mirroring portals (new)
	};
	return Maps;
}

void ANEGameMode::OpenDemo(UObject* WorldContext, int32 Index)
{
	if (GetDemoMaps().IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Display, TEXT("[NE] opening demo %d: %s"), Index + 1, *GetDemoMaps()[Index]);
		UGameplayStatics::OpenLevel(WorldContext, FName(*GetDemoMaps()[Index]));
	}
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GDemoCommand(
		TEXT("NE.Demo"), TEXT("Open non-Euclidean demo 1-8 (same as keys 1-8)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 1 && World)
			{
				ANEGameMode::OpenDemo(World, FCString::Atoi(*Args[0]) - 1);
			}
		}));
}

ANEGameMode::ANEGameMode()
{
	DefaultPawnClass = ANEPlayerCharacter::StaticClass();

	static ConstructorHelpers::FClassFinder<APlayerController> ControllerFinder(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController"));
	if (ControllerFinder.Succeeded())
	{
		PlayerControllerClass = ControllerFinder.Class;
	}

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only ticks for -NESpin / -NEStrafe
}

void ANEGameMode::BeginPlay()
{
	Super::BeginPlay();

	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("NEScreenshot")) || FParse::Value(Cmd, TEXT("NEScreenshot="), ScreenshotSettleSeconds))
	{
		FParse::Value(Cmd, TEXT("NESpin="), SpinDegreesPerSecond);
		FParse::Value(Cmd, TEXT("NEStrafe="), StrafeCmPerSecond);
		FParse::Value(Cmd, TEXT("NEForward="), ForwardCmPerSecond);
		FParse::Value(Cmd, TEXT("NEMotionFrames="), MotionFrameCount);
		bSequence = FParse::Param(Cmd, TEXT("NESequence")) || FParse::Value(Cmd, TEXT("NESequenceEvery="), SequenceEvery);
		SequenceEvery = FMath::Max(1, SequenceEvery);
		FString ScriptString;
		if (FParse::Value(Cmd, TEXT("NEScript="), ScriptString, /*bShouldStopOnSeparator*/ false))
		{
			TArray<FString> Segments;
			ScriptString.ParseIntoArray(Segments, TEXT(";"));
			MotionFrameCount = 0;
			for (const FString& Segment : Segments)
			{
				TArray<FString> Parts;
				Segment.ParseIntoArray(Parts, TEXT(":"));
				if (Parts.Num() == 3)
				{
					Script.Add({FCString::Atoi(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])});
					MotionFrameCount += Script.Last().Frames;
				}
			}
		}
		bUseInput = FParse::Param(Cmd, TEXT("NEUseInput"));
		bProfile = FParse::Param(Cmd, TEXT("NEProfile"));
		FString Poses;
		if (FParse::Value(Cmd, TEXT("NEPoses="), Poses, /*bShouldStopOnSeparator*/ false))
		{
			Poses.ParseIntoArray(PoseList, TEXT("|"));
		}
		GetWorldTimerManager().SetTimer(ScreenshotTimer, this, &ANEGameMode::PollScreenshot, 0.25f, true);
	}
}

void ANEGameMode::ApplyPoseFromCommandLine()
{
	bPoseApplied = true;

	float StartScale = 1.0f;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ANEPlayerCharacter* Character = PC ? Cast<ANEPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (FParse::Value(FCommandLine::Get(), TEXT("NEScale="), StartScale) && Character && StartScale > 0.0f)
	{
		Character->GetPortalTraversal()->ApplyScaleFactor(StartScale / Character->GetActorScale3D().Z);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("NEMirrored")) && Character)
	{
		Character->GetPortalTraversal()->SetMirrored(true); // as if through a mirroring portal
	}

	FString PoseString;
	if (FParse::Value(FCommandLine::Get(), TEXT("NEPose="), PoseString, /*bShouldStopOnSeparator*/ false))
	{
		ApplyPose(PoseString);
	}
}

bool ANEGameMode::ApplyPose(const FString& PoseString)
{
	TArray<FString> Parts;
	PoseString.ParseIntoArray(Parts, TEXT(","));
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ANEPlayerCharacter* Character = PC ? Cast<ANEPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (Parts.Num() != 5 || !Character)
	{
		UE_LOG(LogTemp, Error, TEXT("[NE] a pose needs X,Y,Z,Yaw,Pitch and an NEPlayerCharacter (got '%s')"), *PoseString);
		return false;
	}

	const FVector Eye(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
	const FRotator Rotation(FCString::Atod(*Parts[4]), FCString::Atod(*Parts[3]), 0.0);

	Character->GetCharacterMovement()->DisableMovement();
	const FVector CameraOffset = Character->GetCamera()->GetComponentLocation() - Character->GetActorLocation();
	Character->SetActorLocation(Eye - CameraOffset, false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetPortalTraversal()->ResetTracking(); // this jump isn't a walk through any portal
	PC->SetControlRotation(Rotation);
	UE_LOG(LogTemp, Display, TEXT("[NE] pose applied: eye=%s rot=%s"), *Eye.ToString(), *Rotation.ToString());
	return true;
}

void ANEGameMode::PollScreenshot()
{
	const double Now = GetWorld()->GetRealTimeSeconds();

	if (!bPoseApplied)
	{
		// Automated shots must not depend on whatever the real mouse/keyboard do meanwhile.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->SetIgnoreLookInput(true);
			PC->SetIgnoreMoveInput(true);
		}
		ApplyPoseFromCommandLine();
	}

	if (bScreenshotTaken)
	{
		if (Now - ScreenshotReadyTime > 3.0) // give the screenshot time to be written
		{
			GetWorldTimerManager().ClearTimer(ScreenshotTimer);
			FPlatformMisc::RequestExit(false, TEXT("NEScreenshot"));
		}
		return;
	}
	if (bMotionActive)
	{
		return; // Tick takes the shot
	}

	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		ScreenshotReadyTime = -1.0;
		return;
	}
	if (ScreenshotReadyTime < 0.0)
	{
		ScreenshotReadyTime = Now;
		return;
	}
	if (Now - ScreenshotReadyTime < ScreenshotSettleSeconds)
	{
		return;
	}

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PoseList.IsEmpty())
	{
		PoseListIndex = 0;
		bMotionActive = true; // Tick steps through the poses, one per frame
		SetActorTickEnabled(true);
		return;
	}
	if ((SpinDegreesPerSecond != 0.0f || StrafeCmPerSecond != 0.0f || ForwardCmPerSecond != 0.0f || !Script.IsEmpty()) && PC && PC->GetPawn())
	{
		UCharacterMovementComponent* Movement = PC->GetPawn()->FindComponentByClass<UCharacterMovementComponent>();
		if (bUseInput)
		{
			Movement->SetMovementMode(MOVE_Walking); // -NEPose froze it
		}
		else
		{
			Movement->DisableMovement();
		}
		MotionBaseRotation = PC->GetControlRotation();
		MotionBaseLocation = PC->GetPawn()->GetActorLocation();
		MotionFrames = 0;
		bMotionActive = true;
		SetActorTickEnabled(true);
		UE_LOG(LogTemp, Display, TEXT("[NE] motion: %.1f deg/s, %.1f cm/s strafe, %.1f cm/s forward for %d frames"),
			SpinDegreesPerSecond, StrafeCmPerSecond, ForwardCmPerSecond, MotionFrameCount);
		return;
	}

	TakeScreenshot(TEXT(""), true);
}

void ANEGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!bMotionActive || bScreenshotTaken || !PC || !PC->GetPawn())
	{
		return;
	}

	if (PoseListIndex != INDEX_NONE)
	{
		// Pose and screenshot in the same frame: the portals render from this pose before the shot.
		ApplyPose(PoseList[PoseListIndex]);
		const bool bLastPose = PoseListIndex == PoseList.Num() - 1;
		TakeScreenshot(FString::Printf(TEXT("_pose%02d"), PoseListIndex), bLastPose);
		++PoseListIndex;
		return;
	}

	if (bProfile)
	{
		// The thread/GPU timers describe the previous frame, as in "stat unit".
		const double Now = FPlatformTime::Seconds();
		if (LastFrameTime > 0.0)
		{
			const float FrameMs = static_cast<float>((Now - LastFrameTime) * 1000.0);
			ProfileFrameMs.Add(FrameMs);
			if (FrameMs > 1000.0f / 60.0f)
			{
				const UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>();
				UE_LOG(LogTemp, Display, TEXT("[NE] profile slow frame %d: %.2f ms (captures %d)"),
					MotionFrames, FrameMs, Portals ? Portals->GetLastCaptureCount() : -1);
			}
			ProfileGameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
			ProfileRenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			ProfileGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
			if (const UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>())
			{
				ProfileCaptures.Add(static_cast<float>(Portals->GetLastCaptureCount()));
			}
		}
		LastFrameTime = Now;
	}

	++MotionFrames;
	if (!Script.IsEmpty())
	{
		int32 Start = 0;
		for (const FScriptSegment& Segment : Script)
		{
			if (MotionFrames <= Start + Segment.Frames)
			{
				ForwardCmPerSecond = Segment.Forward;
				SpinDegreesPerSecond = Segment.Spin;
				break;
			}
			Start += Segment.Frames;
		}
	}
	APawn* Pawn = PC->GetPawn();
	FRotator Rotation = PC->GetControlRotation();
	if (bUseInput && PC->PlayerCameraManager)
	{
		// What APlayerController::UpdateRotation does with mouse input (look input itself is
		// ignored during automated shots), including APortalPlayerCameraManager::ProcessViewRotation.
		FRotator DeltaRot(0.0, SpinDegreesPerSecond * MotionStep, 0.0);
		PC->PlayerCameraManager->ProcessViewRotation(static_cast<float>(MotionStep), Rotation, DeltaRot);
		PC->SetControlRotation(Rotation);
	}
	else
	{
		Rotation.Yaw += SpinDegreesPerSecond * MotionStep;
		PC->SetControlRotation(Rotation);
	}
	const FRotationMatrix Yaw(FRotator(0.0, Rotation.Yaw, 0.0));
	const double Step = MotionStep * Pawn->GetActorScale3D().Z;
	if (bUseInput)
	{
		// bForce: the controller ignores move input during automated shots.
		Pawn->AddMovementInput(Yaw.GetUnitAxis(EAxis::X), FMath::Sign(ForwardCmPerSecond), /*bForce*/ true);
		Pawn->AddMovementInput(Yaw.GetUnitAxis(EAxis::Y), FMath::Sign(StrafeCmPerSecond), /*bForce*/ true);
	}
	else
	{
		const FVector Delta = (Yaw.GetUnitAxis(EAxis::X) * ForwardCmPerSecond + Yaw.GetUnitAxis(EAxis::Y) * StrafeCmPerSecond) * Step;
		Pawn->SetActorLocation(Pawn->GetActorLocation() + Delta, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Screenshots are captured at the end of this frame, after portal traversal and rendering.
	const bool bLast = MotionFrames == MotionFrameCount;
	if (bSequence && (MotionFrames % SequenceEvery == 0 || bLast))
	{
		TakeScreenshot(FString::Printf(TEXT("_%03d"), MotionFrames), bLast);
	}
	else if (bLast)
	{
		TakeScreenshot(TEXT(""), true);
	}
}

void ANEGameMode::TakeScreenshot(const FString& Suffix, bool bFinal)
{
	const FString File = FPaths::ConvertRelativePathToFull(
		FPaths::ScreenShotDir() / FString::Printf(TEXT("NE_%s%s.png"), *GetWorld()->GetMapName(), *Suffix));
	FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogTemp, Display, TEXT("[NE] screenshot requested: %s"), *File);

	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		const UPortalTraversalComponent* Traversal = UPortalTraversalComponent::FindForPlayer(PC);
		UE_LOG(LogTemp, Display, TEXT("[NE] pawn=%s view=%s rot=%s scale=%.4f mirrored=%d camera manager=%s"),
			*GetNameSafe(PC->GetPawn()), *ViewLoc.ToString(), *ViewRot.ToString(),
			PC->GetPawn() ? PC->GetPawn()->GetActorScale3D().Z : 0.0, Traversal && Traversal->IsMirrored() ? 1 : 0,
			*GetNameSafe(PC->PlayerCameraManager));
	}
	if (const UPortalSubsystem* Portals = GetWorld()->GetSubsystem<UPortalSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[NE] portal captures last frame: %d"), Portals->GetLastCaptureCount());
	}

	if (bFinal && bProfile)
	{
		LogProfile();
	}
	if (bFinal)
	{
		bScreenshotTaken = true;
		ScreenshotReadyTime = GetWorld()->GetRealTimeSeconds();
	}
}

void ANEGameMode::LogProfile() const
{
	auto Summary = [](const TCHAR* Name, TArray<float> Values)
	{
		if (Values.IsEmpty())
		{
			return;
		}
		Values.Sort();
		double Sum = 0.0;
		for (float V : Values)
		{
			Sum += V;
		}
		const float P95 = Values[FMath::Min(Values.Num() - 1, FMath::FloorToInt(Values.Num() * 0.95f))];
		UE_LOG(LogTemp, Display, TEXT("[NE] profile %-9s mean %7.2f  p95 %7.2f  max %7.2f  (%d frames)"),
			Name, Sum / Values.Num(), P95, Values.Last(), Values.Num());
	};
	Summary(TEXT("frame ms"), ProfileFrameMs);
	Summary(TEXT("game ms"), ProfileGameMs);
	Summary(TEXT("render ms"), ProfileRenderMs);
	Summary(TEXT("gpu ms"), ProfileGpuMs);
	Summary(TEXT("captures"), ProfileCaptures);
}
