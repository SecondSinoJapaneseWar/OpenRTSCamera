#include "UI/RTSUnitIconWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"

void URTSUnitIconWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!UnitIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSUnitIconWidget: 'UnitIcon' (Image) is NOT bound! Check your WBP naming. Expecting variable named 'UnitIcon'."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("RTSUnitIconWidget: NativeConstruct - UnitIcon is bound."));
	}
}

void URTSUnitIconWidget::InitData(const FRTSUnitData& Data, bool bShowIcon, bool bShowBars)
{
	// Set Icon
	if (UnitIcon)
	{
		if (!bShowIcon)
		{
			UnitIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			UnitIcon->SetVisibility(ESlateVisibility::Visible);
			
			if (Data.Icon)
			{
				UnitIcon->SetBrushFromTexture(Data.Icon);
				// Reset color to white (in case it was tinted differently)
				UnitIcon->SetColorAndOpacity(FLinearColor::White);
			}
			else
			{
				// No specific icon data? Show default (White square as user expects, or BP default)
				// We don't change the brush, so it keeps the Designer's default.
				// Optionally set a debug color?
				UE_LOG(LogTemp, Warning, TEXT("RTSUnitIconWidget: Data.Icon is null for %s. Showing default placeholder."), *Data.Name);
			}
		}
	}

	// Update Status Bars
	if (bShowBars)
	{
		UpdateBar(HealthBar, Data.Health, Data.MaxHealth);
		UpdateBar(EnergyBar, Data.Energy, Data.MaxEnergy);
		UpdateBar(ShieldBar, Data.Shield, Data.MaxShield);
	}
	else
	{
		if(HealthBar) HealthBar->SetVisibility(ESlateVisibility::Collapsed);
		if(EnergyBar) EnergyBar->SetVisibility(ESlateVisibility::Collapsed);
		if(ShieldBar) ShieldBar->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Store for Interaction
	StoredData = Data;

	// Tooltip
	FString Tooltip = Data.Name;
	if (Data.MaxHealth > 0) Tooltip += FString::Printf(TEXT("\nHP: %.0f/%.0f"), Data.Health, Data.MaxHealth);
	if (Data.MaxEnergy > 0) Tooltip += FString::Printf(TEXT("\nMP: %.0f/%.0f"), Data.Energy, Data.MaxEnergy);
	if (Data.MaxShield > 0) Tooltip += FString::Printf(TEXT("\nSP: %.0f/%.0f"), Data.Shield, Data.MaxShield);
	SetToolTipText(FText::FromString(Tooltip));
}

void URTSUnitIconWidget::SetIsActive(bool bActive)
{
	// Visual feedback for Active vs Inactive group
	// Starcraft style: Inactive groups are dimmed.
	SetRenderOpacity(bActive ? 1.0f : 0.3f);
}

void URTSUnitIconWidget::UpdateBar(UProgressBar* Bar, float Current, float Max)
{
	if (!Bar) return;

	if (Max > 0.0f)
	{
		Bar->SetPercent(FMath::Clamp(Current / Max, 0.0f, 1.0f));
		Bar->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		Bar->SetVisibility(ESlateVisibility::Collapsed);
	}
}

FReply URTSUnitIconWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Check for Left Click
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
				{
					// --- Starcraft Logic ---
					
					// Shift + Click = Remove (Exclude)
					if (InMouseEvent.IsShiftDown())
					{
						Subsystem->RemoveUnit(StoredData);
						return FReply::Handled();
					}

					// Ctrl + Click = Select Type (Keep only this group)
					if (InMouseEvent.IsControlDown())
					{
						Subsystem->SelectGroup(StoredData.Name);
						return FReply::Handled();
					}

					// Normal Click = Select This Unit (Exclusive)
					// We need to construct a single selection.
					TArray<AActor*> NewActors;
					TArray<FEntityHandle> NewEntities;
					
					if (StoredData.ActorPtr) NewActors.Add(StoredData.ActorPtr);
					if (StoredData.EntityHandle.Index > 0) NewEntities.Add(StoredData.EntityHandle);
					
					// If Summary Item (Count > 1), normal click usually Selects the GROUP?
					// In SC2: 
					// - Wireframe (List): Click selects unit.
					// - Summary: Click selects ALL of that type (same as Ctrl+Click in Wireframe).
					// If Count > 1, treating as Ctrl+Click (Group Select).
					
					if (StoredData.Count > 1)
					{
						Subsystem->SelectGroup(StoredData.Name);
					}
					else
					{
						Subsystem->SetSelectedUnits(NewActors, NewEntities, ERTSSelectionModifier::Replace);
					}
					
					return FReply::Handled();
				}
			}
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
