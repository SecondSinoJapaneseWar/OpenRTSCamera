// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSUnitIconWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * Represents a single unit icon or group summary icon in the selection panel.
 */
UCLASS()
class OPENRTSCAMERA_API URTSUnitIconWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Updates the widget with data.
	 * @param bShowIcon  If true, forces icon visibility (if valid). If false, hides icon.
	 * @param bShowBars  If true, shows status bars.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void InitData(const FRTSUnitData& Data, bool bShowIcon = true, bool bShowBars = true);

	/**
	 * Sets the visual active state (e.g. for Tab toggling).
	 * Active: Default appearance.
	 * Inactive: Dimmed opacity.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetIsActive(bool bActive);

protected:
	virtual void NativeConstruct() override;

	// Input Handling
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// UI Bindings
	UPROPERTY(meta = (BindWidgetOptional))
	class UImage* UnitIcon;

	// -- Status Bars (Optional) --

	// -- Status Bars (Optional) --
	// If the unit has valid MaxHealth/Energy/Shield, these bars will update. 
	// Otherwise they will be hidden.

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* EnergyBar;

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* ShieldBar;

private:
	// Internal Copy of Data for Interaction
	FRTSUnitData StoredData;

	void UpdateBar(class UProgressBar* Bar, float Current, float Max);
};
