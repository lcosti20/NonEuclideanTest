#include "NEPuzzleHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NEPuzzleGate.h"
#include "NEPuzzleObject.h"
#include "NEPuzzleSocket.h"
#include "PortalGrabComponent.h"
#include "PortalTraversalComponent.h"

void ANEPuzzleHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}

	const FLinearColor Text(0.95f, 0.95f, 0.95f);
	const FLinearColor Dim(0.75f, 0.75f, 0.8f);
	const FLinearColor Good(0.3f, 1.0f, 0.4f);
	const FLinearColor Bad(1.0f, 0.45f, 0.35f);
	UFont* Font = GEngine->GetMediumFont();
	const float LineHeight = 22.0f;

	// Crosshair.
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	DrawRect(Text, CX - 1.0f, CY - 7.0f, 2.0f, 14.0f);
	DrawRect(Text, CX - 7.0f, CY - 1.0f, 14.0f, 2.0f);

	float Y = 16.0f;
	auto Line = [&](const FString& String, const FLinearColor& Color)
	{
		DrawText(String, Color, 16.0f, Y, Font);
		Y += LineHeight;
	};

	Line(TEXT("E: pick up / drop    Left click: throw    Keys 1-8: switch maps"), Dim);

	const APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (const UPortalTraversalComponent* Traversal = Pawn ? Pawn->FindComponentByClass<UPortalTraversalComponent>() : nullptr)
	{
		Line(FString::Printf(TEXT("You: size x%.2f%s"), FMath::Abs(Pawn->GetActorScale3D().Z),
			Traversal->IsMirrored() ? TEXT(", mirrored (the world looks flipped)") : TEXT("")), Dim);
	}

	Y += LineHeight * 0.5f;
	for (TActorIterator<ANEPuzzleSocket> It(GetWorld()); It; ++It)
	{
		const ANEPuzzleSocket* Socket = *It;
		const bool bSolved = Socket->IsSolved();
		const bool bWrong = !bSolved && Socket->GetStatus() != TEXT("empty");
		Line(FString::Printf(TEXT("[%s] %s: needs a %s  -  %s"), bSolved ? TEXT("x") : TEXT(" "), *Socket->Label,
			*Socket->DescribeRequirement(), *Socket->GetStatus()), bSolved ? Good : (bWrong ? Bad : Text));
	}
	for (TActorIterator<ANEPuzzleGate> It(GetWorld()); It; ++It)
	{
		if (It->IsOpen())
		{
			Line(TEXT("All sockets solved: the gate is open."), Good);
		}
	}

	// What's held or aimed at, under the crosshair.
	const UPortalGrabComponent* Grab = Pawn ? Pawn->FindComponentByClass<UPortalGrabComponent>() : nullptr;
	if (!Grab)
	{
		return;
	}
	FString Hint;
	if (const UPrimitiveComponent* Held = Grab->GetHeldComponent())
	{
		const ANEPuzzleObject* Object = Cast<ANEPuzzleObject>(Held->GetOwner());
		Hint = Object
			? FString::Printf(TEXT("Holding %s (%s)    E: drop    click: throw"), *Object->Label, *Object->DescribeState())
			: TEXT("Holding an object    E: drop    click: throw");
	}
	else if (const UPrimitiveComponent* Aimed = Grab->GetAimedComponent())
	{
		const ANEPuzzleObject* Object = Cast<ANEPuzzleObject>(Aimed->GetOwner());
		Hint = Object ? FString::Printf(TEXT("E: pick up %s (%s)"), *Object->Label, *Object->DescribeState()) : TEXT("E: pick up");
	}
	if (!Hint.IsEmpty())
	{
		float Width, Height;
		GetTextSize(Hint, Width, Height, Font);
		DrawText(Hint, Text, CX - Width * 0.5f, CY + 24.0f, Font);
	}
}
