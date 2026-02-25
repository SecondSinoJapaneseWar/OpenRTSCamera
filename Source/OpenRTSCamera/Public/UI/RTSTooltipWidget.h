// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/RTSCommandButton.h"
#include "RTSTooltipWidget.generated.h"

class UTextBlock;
class UImage;

/**
 * A Rich Tooltip for RTS Commands.
 * Displays Name, Description, Cost, Cooldown.
 */
UCLASS()
class OPENRTSCAMERA_API URTSTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	// Update Tooltip UI from Data
	UFUNCTION(BlueprintCallable, Category = "RTS Tooltip")
	void UpdateTooltip(URTSCommandButton* Data);

protected:

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class URichTextBlock> DescriptionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CostText; // "100 M / 50 G"

	// Optional icon
    UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

    // --- Style Config (Start) ---
    // User requested "Roboto 32". We can force this here or just expose it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    int32 DefaultFontSize = 32;
    
    virtual void NativeConstruct() override;
};
