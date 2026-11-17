#include "NEMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	void SetFontSize(UTextBlock* Text, int32 Size)
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
	}
}

void UNEMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetAnchorsInViewport(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));

	TitleText = WidgetTree->ConstructWidget<UTextBlock>();
	SetFontSize(TitleText, 40);
	LinesBox = WidgetTree->ConstructWidget<UVerticalBox>();
	FooterText = WidgetTree->ConstructWidget<UTextBlock>();
	SetFontSize(FooterText, 20);
	FooterText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.75f)));

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
	Column->AddChildToVerticalBox(LinesBox);
	Column->AddChildToVerticalBox(FooterText)->SetPadding(FMargin(0.0f, 24.0f, 0.0f, 0.0f));

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
	Background->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	Background->SetPadding(FMargin(48.0f));
	Background->SetContent(Column);
	WidgetTree->RootWidget = Background;
}

void UNEMenuWidget::SetContent(const FString& Title, const TArray<FString>& Lines, const FString& Footer)
{
	TitleText->SetText(FText::FromString(Title));
	FooterText->SetText(FText::FromString(Footer));

	LinesBox->ClearChildren();
	for (const FString& Line : Lines)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		SetFontSize(Text, 24);
		Text->SetText(FText::FromString(Line));
		LinesBox->AddChildToVerticalBox(Text)->SetPadding(FMargin(0.0f, 4.0f));
	}
}
