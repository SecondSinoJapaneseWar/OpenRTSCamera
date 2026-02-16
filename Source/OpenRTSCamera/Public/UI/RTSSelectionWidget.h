// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSSelectionWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class URTSUnitIconWidget;
class UProgressBar;

/**
 * Main Selection Panel. Handles Single, List, and Summary views.
 */
UCLASS()
class OPENRTSCAMERA_API URTSSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	UFUNCTION()
	void OnSelectionUpdated(const FRTSSelectionView& View);

	/**
	* Class of the item widget to spawn in the list.
	* Must be set in Blueprint (WBP_RTSUnitIcon).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<URTSUnitIconWidget> UnitIconClass;

	// The class to use for each unit icon. 
	// If set in Editor, we use this. If nullptr, we try to detect from the first child in Designer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<UUserWidget> IconWidgetClass;

	// Optional: The class for the "Count" widget in Summary mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<UUserWidget> CountWidgetClass;

	// -- Bind Widgets --
	
	/**
	 * Max items to show in the grid. 
	 * Calculated automatically as MaxRows * MaxColumns.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS Selection")
	int32 ItemsPerPage = 12;

	/**
	* Max columns for the grid. Defaults to 6.
	* If a GridPanel template is detected, this will be overwritten by the max column index found.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 MaxColumns = 6;

	/**
	* Max rows for the grid. Defaults to 2.
	* If a GridPanel template is detected, this will be overwritten by the max row index found.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 MaxRows = 2;

	// Container for the icons (e.g., WrapBox or GridPanel)
	// User should place template widgets inside this in the Editor.
	// Child 0: Unit Icon Template
	// Child 1 (Optional): Count Text Template
	UPROPERTY(meta = (BindWidget))
	UPanelWidget* IconContainer;

	// Optional: Container to show when only 1 unit is selected.
	// If bound, this will be Visible in Single Mode, and Collapsed in List/Summary Mode.
	// The IconContainer will be the inverse.
	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* SingleUnitDetail;

private:
	// Pool of re-usable icon widgets
	UPROPERTY()
	TArray<URTSUnitIconWidget*> IconSlots;

	// Pool of re-usable count widgets (for Summary mode)
	UPROPERTY()
	TArray<UTextBlock*> CountSlots;

	void RefreshGrid(const FRTSSelectionView& View);
};
