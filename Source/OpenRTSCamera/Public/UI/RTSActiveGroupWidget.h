// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSSelectionSubsystem.h"
#include "RTSActiveGroupWidget.generated.h"

class URTSUnitIconWidget;

/**
 * A standalone widget that displays the currently active sub-group (Leader/Avatar).
 * Listen to RTSSelectionSubsystem directly.
 */
UCLASS(BlueprintType, Blueprintable)
class OPENRTSCAMERA_API URTSActiveGroupWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	virtual void OnSelectionUpdated(const FRTSSelectionView& View);

	// Optional: If bound, we forward the data to this internal widget.
	// This allows users to wrap our logic in a text/border/etc.
	// Or users can just inherit this class in their WBP_Avatar
	UPROPERTY(meta = (BindWidgetOptional))
	URTSUnitIconWidget* GroupIcon;
	
	// Optional: A text block for the name? (Or let GroupIcon handle it?)
	// Let's keep it simple: It mostly wraps functionality.

	/**
	 * Event fired when the active group data changes.
	 * Implement this in Blueprint to add custom logic (e.g. update 3D Avatar, play sound).
	 * @param Data The data of the active unit/group.
	 * @param bHasData True if there is a valid selection, False if empty.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "RTS Selection")
	void OnActiveGroupChanged(const FRTSUnitData& Data, bool bHasData);
};
