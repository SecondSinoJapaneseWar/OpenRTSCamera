// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSActiveGroupWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h" 

void URTSActiveGroupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.AddDynamic(this, &URTSActiveGroupWidget::OnSelectionUpdated);
			}
		}
	}
}

void URTSActiveGroupWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	const FRTSUnitData* ActiveData = nullptr;
	FString ActiveKey = View.ActiveGroupKey;
	
	if (!ActiveKey.IsEmpty())
	{
		ActiveData = View.Items.FindByPredicate([&](const FRTSUnitData& Item) {
			return Item.Name == ActiveKey;
		});
	}

	// Fallback: If no ActiveKey but items exist (e.g. Single Mode), use first item
	if (!ActiveData && View.Items.Num() > 0)
	{
		ActiveData = &View.Items[0];
	}

	if (ActiveData)
	{
		// We have an active group/unit.
		// If we wrap an internal icon widget, update it.
		if (GroupIcon)
		{
			// Show Icon, Show Bars
			GroupIcon->InitData(*ActiveData, true, true);
			GroupIcon->SetIsActive(true);
		}
		
		// Ensure self is visible (hit test invisible to allow tooltips on children)
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		
		// Notify BP
		OnActiveGroupChanged(*ActiveData, true);
	}
	else
	{
		// No selection at all.
		SetVisibility(ESlateVisibility::Hidden);
		
		// Notify BP (Empty Data)
		OnActiveGroupChanged(FRTSUnitData(), false);
	}
}
