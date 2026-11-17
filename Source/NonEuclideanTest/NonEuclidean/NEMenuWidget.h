#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NEMenuWidget.generated.h"

class UTextBlock;
class UVerticalBox;

/**
 * A full-screen panel for the start menu and the Esc prompt: a title, lines of text and a footer.
 * Built in code, so it needs no Blueprint.
 */
UCLASS()
class NONEUCLIDEANTEST_API UNEMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Replaces the title, the lines and the footer. */
	void SetContent(const FString& Title, const TArray<FString>& Lines, const FString& Footer);

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UVerticalBox> LinesBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> FooterText;
};
