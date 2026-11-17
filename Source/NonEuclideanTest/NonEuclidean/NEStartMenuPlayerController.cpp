#include "NEStartMenuPlayerController.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "NEMenuWidget.h"
#include "NEStartMenuGameMode.h"

void ANEStartMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (const ANEStartMenuGameMode* GameMode = GetWorld()->GetAuthGameMode<ANEStartMenuGameMode>())
	{
		for (const TSoftObjectPtr<UWorld>& Level : GameMode->Levels)
		{
			Levels.Add(Level.ToSoftObjectPath().GetLongPackageName());
		}
	}
	Levels.Sort();

	MenuWidget = CreateWidget<UNEMenuWidget>(this);
	MenuWidget->AddToViewport();
	Refresh();
}

void ANEStartMenuPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	static const FKey NumberKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(NumberKeys); ++Index)
	{
		FInputKeyBinding Binding(FInputChord(NumberKeys[Index]), IE_Pressed);
		Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Index]() { Choose(Index); });
		InputComponent->KeyBindings.Add(Binding);
	}

	FInputKeyBinding UpBinding(FInputChord(EKeys::Up), IE_Pressed);
	UpBinding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { MoveSelection(-1); });
	InputComponent->KeyBindings.Add(UpBinding);

	FInputKeyBinding DownBinding(FInputChord(EKeys::Down), IE_Pressed);
	DownBinding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { MoveSelection(1); });
	InputComponent->KeyBindings.Add(DownBinding);

	FInputKeyBinding EnterBinding(FInputChord(EKeys::Enter), IE_Pressed);
	EnterBinding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this]() { Choose(Selected); });
	InputComponent->KeyBindings.Add(EnterBinding);
}

void ANEStartMenuPlayerController::Refresh()
{
	TArray<FString> Lines;
	for (int32 Index = 0; Index < Levels.Num(); ++Index)
	{
		Lines.Add(FString::Printf(TEXT("%s%d. %s"), Index == Selected ? TEXT("> ") : TEXT("   "), Index + 1,
			*FPaths::GetBaseFilename(Levels[Index])));
	}
	if (Lines.IsEmpty())
	{
		Lines.Add(TEXT("No levels found in /Game/NonEuclidean/Maps"));
	}
	MenuWidget->SetContent(TEXT("Non-Euclidean levels"), Lines, TEXT("Up/Down to choose    Enter or 1-9 to play"));
}

void ANEStartMenuPlayerController::MoveSelection(int32 Delta)
{
	if (Levels.Num() > 0)
	{
		Selected = FMath::Clamp(Selected + Delta, 0, Levels.Num() - 1);
		Refresh();
	}
}

void ANEStartMenuPlayerController::Choose(int32 Index)
{
	if (Levels.IsValidIndex(Index))
	{
		UGameplayStatics::OpenLevel(this, FName(*Levels[Index]));
	}
}
