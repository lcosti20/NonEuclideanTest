#include "NEPuzzleGameMode.h"

#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "NEPlayerCharacter.h"
#include "NEPuzzleGate.h"
#include "NEPuzzleHUD.h"
#include "NEPuzzleObject.h"
#include "NEPuzzleSocket.h"
#include "Portal.h"
#include "PortalGrabComponent.h"
#include "PortalTraversalComponent.h"

ANEPuzzleGameMode::ANEPuzzleGameMode()
{
	HUDClass = ANEPuzzleHUD::StaticClass();
}

void ANEPuzzleGameMode::BeginPlay()
{
	Super::BeginPlay();
	FString Places;
	if (FParse::Value(FCommandLine::Get(), TEXT("NEPlace="), Places, /*bShouldStopOnSeparator*/ false))
	{
		PlaceObjects(Places);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("NEPlaceLater="), Places, /*bShouldStopOnSeparator*/ false))
	{
		GetWorldTimerManager().SetTimer(PlaceLaterTimer, FTimerDelegate::CreateWeakLambda(this, [this, Places]() { PlaceObjects(Places); }), 1.0f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("NEPuzzleTest")))
	{
		bSelfTest = true;
		BuildSelfTest();
		SetActorTickEnabled(true);
	}
}

ANEPuzzleObject* ANEPuzzleGameMode::FindObject(const FString& Label) const
{
	for (TActorIterator<ANEPuzzleObject> It(GetWorld()); It; ++It)
	{
		if (It->Label == Label)
		{
			return *It;
		}
	}
	return nullptr;
}

ANEPuzzleSocket* ANEPuzzleGameMode::FindSocket(const FString& Label) const
{
	for (TActorIterator<ANEPuzzleSocket> It(GetWorld()); It; ++It)
	{
		if (It->Label == Label)
		{
			return *It;
		}
	}
	return nullptr;
}

APortal* ANEPuzzleGameMode::FindPortal(const FString& Label) const
{
	for (TActorIterator<APortal> It(GetWorld()); It; ++It)
	{
		if (It->GetActorNameOrLabel() == Label)
		{
			return *It;
		}
	}
	return nullptr;
}

void ANEPuzzleGameMode::PlaceObjects(const FString& Places)
{
	TArray<FString> Entries;
	Places.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		FString Label, Numbers;
		TArray<FString> Parts;
		if (!Entry.Split(TEXT(":"), &Label, &Numbers) || Numbers.ParseIntoArray(Parts, TEXT(",")) != 5)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NE] bad -NEPlace entry '%s'"), *Entry);
			continue;
		}
		ANEPuzzleObject* Object = FindObject(Label);
		if (!Object)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NE] -NEPlace: no object labelled '%s'"), *Label);
			continue;
		}
		const double Size = FCString::Atod(*Parts[4]);
		const FVector Scale(Size, FMath::Abs(Size), FMath::Abs(Size));
		const FVector Location(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		Object->PlaceAt(FTransform(FRotator(0.0, FCString::Atod(*Parts[3]), 0.0), Location, Scale), /*bFreeze*/ true);
		UE_LOG(LogTemp, Display, TEXT("[NE] placed %s at %s (%s)"), *Label, *Location.ToString(), *Object->DescribeState());
	}
}

// --------------------------------------------------------------------------- self-test

void ANEPuzzleGameMode::Check(bool bPassed, const FString& What, const FString& Detail)
{
	(bPassed ? Passed : Failed)++;
	UE_LOG(LogTemp, Display, TEXT("[NE] PuzzleTest %s: %s (%s)"), bPassed ? TEXT("PASS") : TEXT("FAIL"), *What, *Detail);
}

void ANEPuzzleGameMode::Do(TFunction<void()> Action)
{
	Steps.Add([Action](float) { Action(); return true; });
}

void ANEPuzzleGameMode::WaitSeconds(float Seconds)
{
	Steps.Add([Seconds](float Elapsed) { return Elapsed >= Seconds; });
}

void ANEPuzzleGameMode::WaitUntil(const FString& What, TFunction<bool()> Condition, float Timeout)
{
	Steps.Add([this, What, Condition, Timeout](float Elapsed)
	{
		if (Condition())
		{
			return true;
		}
		if (Elapsed > Timeout)
		{
			Check(false, What, FString::Printf(TEXT("timed out after %.1f s"), Timeout));
			return true;
		}
		return false;
	});
}

void ANEPuzzleGameMode::Expect(const FString& What, TFunction<bool()> Condition, TFunction<FString()> Detail)
{
	Do([this, What, Condition, Detail]() { Check(Condition(), What, Detail()); });
}

void ANEPuzzleGameMode::ThrowThrough(const FString& ObjectLabel, const FString& PortalLabel, float Speed)
{
	struct FState { int32 Count = 0; float Size = 0.0f; bool bMirrored = false; };
	TSharedRef<FState> State = MakeShared<FState>();
	const FString What = FString::Printf(TEXT("throw %s through %s"), *ObjectLabel, *PortalLabel);

	Do([this, ObjectLabel, PortalLabel, Speed, State]()
	{
		ANEPuzzleObject* Object = FindObject(ObjectLabel);
		const APortal* Portal = FindPortal(PortalLabel);
		if (!Object || !Portal)
		{
			return;
		}
		const FVector Normal = Portal->GetNormal();
		const FVector Start = Portal->GetActorLocation() + Normal * 60.0;
		const FMatrix Warp = Portal->GetWarpTo(Portal->GetLinkFor(Start));
		State->Count = Object->GetPortalTraversal()->GetTraversalCount();
		State->Size = Object->GetSize() * static_cast<float>(Warp.TransformVector(FVector::XAxisVector).Size());
		State->bMirrored = Object->IsMirrored() != (Warp.Determinant() < 0.0);
		Object->PlaceAt(FTransform(FQuat::Identity, Start, Object->GetActorScale3D()), /*bFreeze*/ false);
		Object->GetMesh()->SetPhysicsLinearVelocity(-Normal * Speed);
	});
	WaitUntil(What + TEXT(": passes through"), [this, ObjectLabel, State]()
	{
		const ANEPuzzleObject* Object = FindObject(ObjectLabel);
		return Object && Object->GetPortalTraversal()->GetTraversalCount() > State->Count;
	}, 3.0f);
	WaitSeconds(0.5f);
	Expect(What, [this, ObjectLabel, State]()
	{
		const ANEPuzzleObject* Object = FindObject(ObjectLabel);
		return Object && FMath::IsNearlyEqual(Object->GetSize(), State->Size, 0.01f) && Object->IsMirrored() == State->bMirrored;
	}, [this, ObjectLabel, State]()
	{
		const ANEPuzzleObject* Object = FindObject(ObjectLabel);
		return FString::Printf(TEXT("now %s, expected size x%.2f%s"), Object ? *Object->DescribeState() : TEXT("missing"),
			State->Size, State->bMirrored ? TEXT(", mirrored") : TEXT(""));
	});
}

void ANEPuzzleGameMode::PutInSocket(const FString& ObjectLabel, const FString& SocketLabel, bool bExpectSolved)
{
	const FString What = FString::Printf(TEXT("%s %s %s"), bExpectSolved ? TEXT("put") : TEXT("refuse"), *ObjectLabel, *SocketLabel);
	Do([this, ObjectLabel, SocketLabel]()
	{
		ANEPuzzleObject* Object = FindObject(ObjectLabel);
		const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
		if (Object && Socket)
		{
			// Dropped from just above the ghost, keeping the object's own size and handedness.
			const FTransform Ghost = Socket->GetGhostTransform();
			Object->PlaceAt(FTransform(Ghost.GetRotation(), Ghost.GetLocation() + FVector(0.0, 0.0, 15.0), Object->GetActorScale3D()), false);
		}
	});
	if (bExpectSolved)
	{
		WaitUntil(What + TEXT(": solved"), [this, SocketLabel]()
		{
			const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
			return Socket && Socket->IsSolved();
		}, 3.0f);
		Expect(What, [this, SocketLabel]()
		{
			const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
			return Socket && Socket->IsSolved();
		}, [this, SocketLabel]()
		{
			const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
			return Socket ? Socket->GetStatus() : FString(TEXT("missing"));
		});
	}
	else
	{
		WaitSeconds(1.0f);
		Expect(What, [this, SocketLabel]()
		{
			const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
			return Socket && !Socket->IsSolved() && Socket->GetStatus().Contains(TEXT("wrong"));
		}, [this, SocketLabel]()
		{
			const ANEPuzzleSocket* Socket = FindSocket(SocketLabel);
			return Socket ? Socket->GetStatus() : FString(TEXT("missing"));
		});
	}
}

void ANEPuzzleGameMode::WalkForwardThroughPortal(const FString& What)
{
	struct FState { int32 Count = -1; double CrossedAt = -1.0; };
	TSharedRef<FState> State = MakeShared<FState>();
	Steps.Add([this, What, State](float Elapsed)
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UPortalTraversalComponent* Traversal = Pawn ? Pawn->FindComponentByClass<UPortalTraversalComponent>() : nullptr;
		if (!Traversal)
		{
			return true;
		}
		if (State->Count < 0)
		{
			State->Count = Traversal->GetTraversalCount();
		}
		const double Now = GetWorld()->GetTimeSeconds();
		if (State->CrossedAt < 0.0 && Traversal->GetTraversalCount() > State->Count)
		{
			State->CrossedAt = Now;
		}
		if (State->CrossedAt >= 0.0 && Now - State->CrossedAt > 0.35)
		{
			return true;
		}
		if (Elapsed > 5.0f)
		{
			Check(false, What, TEXT("never reached the portal"));
			return true;
		}
		Pawn->AddMovementInput(Pawn->GetActorForwardVector());
		return false;
	});
}

void ANEPuzzleGameMode::CarryThrough(const FString& ObjectLabel, const FString& PortalLabel)
{
	Do([this, ObjectLabel, PortalLabel]()
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ANEPlayerCharacter* Player = PC ? Cast<ANEPlayerCharacter>(PC->GetPawn()) : nullptr;
		ANEPuzzleObject* Object = FindObject(ObjectLabel);
		const APortal* Portal = FindPortal(PortalLabel);
		if (!Player || !Object || !Portal)
		{
			return;
		}
		// The player 2.6 m in front of the portal facing it, the object on the floor between them.
		const FVector Normal = Portal->GetNormal();
		const FVector Base = Portal->GetActorLocation() * FVector(1.0, 1.0, 0.0);
		const double S = FMath::Abs(Player->GetActorScale3D().Z);
		Player->SetActorLocation(Base + Normal * 260.0 + FVector(0.0, 0.0, 90.0 * S), false, nullptr, ETeleportType::TeleportPhysics);
		Player->GetPortalTraversal()->ResetTracking();
		PC->SetControlRotation((-Normal).Rotation());
		Object->PlaceAt(FTransform(FQuat::Identity, Base + Normal * 150.0 + FVector(0.0, 0.0, 30.0), Object->GetActorScale3D()), false);
	});
	WaitSeconds(0.6f);
	Do([this, ObjectLabel]()
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ANEPlayerCharacter* Player = PC ? Cast<ANEPlayerCharacter>(PC->GetPawn()) : nullptr;
		const ANEPuzzleObject* Object = FindObject(ObjectLabel);
		if (!Player || !Object)
		{
			return;
		}
		PC->SetControlRotation((Object->GetActorLocation() - Player->GetCamera()->GetComponentLocation()).Rotation());
		const bool bGrabbed = Player->GetPortalGrab()->Grab();
		Check(bGrabbed && Player->GetPortalGrab()->GetHeldComponent() == Object->GetMesh(),
			FString::Printf(TEXT("pick up %s"), *ObjectLabel), bGrabbed ? TEXT("held") : TEXT("nothing under the crosshair"));
		PC->SetControlRotation(FRotator(0.0, PC->GetControlRotation().Yaw, 0.0));
	});
	WaitSeconds(0.6f);
	WalkForwardThroughPortal(FString::Printf(TEXT("carry %s through %s"), *ObjectLabel, *PortalLabel));
}

void ANEPuzzleGameMode::BuildSelfTest()
{
	auto Player = [this]() -> ANEPlayerCharacter*
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		return PC ? Cast<ANEPlayerCharacter>(PC->GetPawn()) : nullptr;
	};
	auto PlayerState = [Player]() -> FString
	{
		const ANEPlayerCharacter* P = Player();
		return P ? FString::Printf(TEXT("player size x%.2f%s"), FMath::Abs(P->GetActorScale3D().Z),
			P->GetPortalTraversal()->IsMirrored() ? TEXT(", mirrored") : TEXT("")) : FString(TEXT("no player"));
	};
	auto Holding = [this, Player](const FString& Label)
	{
		const ANEPlayerCharacter* P = Player();
		const ANEPuzzleObject* Object = FindObject(Label);
		return P && Object && P->GetPortalGrab()->GetHeldComponent() == Object->GetMesh();
	};
	auto ObjectState = [this, Player](const FString& Label) -> FString
	{
		const ANEPuzzleObject* Object = FindObject(Label);
		const ANEPlayerCharacter* P = Player();
		return Object ? FString::Printf(TEXT("%s %s, relative to player x%.2f"), *Label, *Object->DescribeState(),
			P ? P->GetPortalGrab()->GetHeldRelativeScale() : 0.0f) : FString(TEXT("missing"));
	};
	auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.01f); };

	WaitSeconds(1.0f); // let everything settle

	// Throwing objects through on their own changes them relative to the world and the player.
	ThrowThrough(TEXT("Spare key"), TEXT("Portal_MirrorA"), 500.0f);   // mirrored
	ThrowThrough(TEXT("Spare key"), TEXT("Portal_ShrinkBig"), 500.0f); // mirrored and half size
	PutInSocket(TEXT("Spare key"), TEXT("Small mirror socket"), true);
	ThrowThrough(TEXT("Crate B"), TEXT("Portal_ShrinkSmall"), 300.0f);  // double size
	PutInSocket(TEXT("Crate B"), TEXT("Big socket"), true);

	// Carrying: the object and the player change together.
	CarryThrough(TEXT("Crate A"), TEXT("Portal_ShrinkBig"));
	Expect(TEXT("carried crate shrinks with the player"), [this, Player, Holding, Near]()
	{
		const ANEPlayerCharacter* P = Player();
		const ANEPuzzleObject* Crate = FindObject(TEXT("Crate A"));
		return P && Crate && Holding(TEXT("Crate A")) && Near(FMath::Abs(P->GetActorScale3D().Z), 0.5f)
			&& Near(Crate->GetSize(), 0.5f) && Near(P->GetPortalGrab()->GetHeldRelativeScale(), 1.0f);
	}, [PlayerState, ObjectState]() { return PlayerState() + TEXT("; ") + ObjectState(TEXT("Crate A")); });
	Do([this]()
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->SetControlRotation(PC->GetControlRotation() + FRotator(0.0, 180.0, 0.0)); // turn round in the small tunnel
		}
	});
	WaitSeconds(0.6f);
	WalkForwardThroughPortal(TEXT("carry Crate A back through Portal_ShrinkSmall"));
	Expect(TEXT("carried crate grows back with the player"), [this, Player, Holding, Near]()
	{
		const ANEPlayerCharacter* P = Player();
		const ANEPuzzleObject* Crate = FindObject(TEXT("Crate A"));
		return P && Crate && Holding(TEXT("Crate A")) && Near(FMath::Abs(P->GetActorScale3D().Z), 1.0f) && Near(Crate->GetSize(), 1.0f);
	}, [PlayerState, ObjectState]() { return PlayerState() + TEXT("; ") + ObjectState(TEXT("Crate A")); });
	Do([Player]() { if (ANEPlayerCharacter* P = Player()) { P->GetPortalGrab()->Release(); } });
	ThrowThrough(TEXT("Crate A"), TEXT("Portal_ShrinkBig"), 500.0f); // half size
	PutInSocket(TEXT("Crate A"), TEXT("Small socket"), true);

	// The mirror socket refuses the key the right way round...
	PutInSocket(TEXT("Mirror key"), TEXT("Mirror socket"), false);
	// ...and takes it once it's been carried through the mirror.
	CarryThrough(TEXT("Mirror key"), TEXT("Portal_MirrorA"));
	Expect(TEXT("carried key is mirrored with the player"), [this, Player, Holding, Near]()
	{
		const ANEPlayerCharacter* P = Player();
		const ANEPuzzleObject* Key = FindObject(TEXT("Mirror key"));
		return P && Key && Holding(TEXT("Mirror key")) && P->GetPortalTraversal()->IsMirrored() && Key->IsMirrored()
			&& Near(Key->GetSize(), 1.0f);
	}, [PlayerState, ObjectState]() { return PlayerState() + TEXT("; ") + ObjectState(TEXT("Mirror key")); });
	Do([Player]() { if (ANEPlayerCharacter* P = Player()) { P->GetPortalGrab()->Release(); } });
	PutInSocket(TEXT("Mirror key"), TEXT("Mirror socket"), true);

	auto GateOpen = [this]()
	{
		for (TActorIterator<ANEPuzzleGate> It(GetWorld()); It; ++It)
		{
			if (!It->IsOpen())
			{
				return false;
			}
		}
		return true;
	};
	WaitUntil(TEXT("gate opens"), GateOpen, 2.0f);
	Expect(TEXT("gate opens once all four sockets are solved"), GateOpen, []() { return FString(TEXT("gate")); });
	Do([this]()
	{
		UE_LOG(LogTemp, Display, TEXT("[NE] PuzzleTest done: %d passed, %d failed"), Passed, Failed);
		FPlatformMisc::RequestExit(false);
	});
}

void ANEPuzzleGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSelfTest || !Steps.IsValidIndex(StepIndex))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (StepStartTime < 0.0)
	{
		StepStartTime = Now;
	}
	// Run steps until one has to wait for a later frame.
	while (Steps.IsValidIndex(StepIndex) && Steps[StepIndex](static_cast<float>(Now - StepStartTime)))
	{
		++StepIndex;
		StepStartTime = Now;
	}
}
