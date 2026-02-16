#include "UI/RTSSelectionWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/WrapBox.h"
#include "Kismet/GameplayStatics.h"

void URTSSelectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Template Extraction Logic
	if (IconContainer)
	{
		int32 FoundMaxRow = 0;
		int32 FoundMaxCol = 0;
		bool bFoundGridSlot = false;
		bool bUsedPanelSettings = false;

		// Strategy 1: Check GridPanel explicit configurations (RowFill / ColumnFill)
		// This is the "Elegant" way if the user configured the Grid structure in Designer.
		if (UGridPanel* GridPanel = Cast<UGridPanel>(IconContainer))
		{
			if (GridPanel->RowFill.Num() > 0)
			{
				FoundMaxRow = GridPanel->RowFill.Num() - 1;
				bFoundGridSlot = true;
				bUsedPanelSettings = true;
			}
			if (GridPanel->ColumnFill.Num() > 0)
			{
				FoundMaxCol = GridPanel->ColumnFill.Num() - 1;
				bFoundGridSlot = true;
				bUsedPanelSettings = true;
			}
		}

		// Strategy 2: Scan Children for Occupancy (Fallback or for UniformGridPanel)
		// If explicit settings were not found, we scan where the user placed the templates.
		if (!bUsedPanelSettings)
		{
			const int32 ChildrenCount = IconContainer->GetChildrenCount();
			for (int32 i = 0; i < ChildrenCount; ++i)
			{
				UWidget* Child = IconContainer->GetChildAt(i);
				if (!Child) continue;

				// --- 1. Class Detection ---
				if (!IconWidgetClass)
				{
					if (URTSUnitIconWidget* IconWidget = Cast<URTSUnitIconWidget>(Child))
					{
						IconWidgetClass = IconWidget->GetClass();
					}
				}
				if (!CountWidgetClass)
				{
					if (UTextBlock* TextBlock = Cast<UTextBlock>(Child))
					{
						CountWidgetClass = TextBlock->GetClass();
					}
				}

				// --- 2. Grid Size Detection ---
				if (UUniformGridSlot* USlot = Cast<UUniformGridSlot>(Child->Slot))
				{
					FoundMaxRow = FMath::Max(FoundMaxRow, USlot->GetRow());
					FoundMaxCol = FMath::Max(FoundMaxCol, USlot->GetColumn());
					bFoundGridSlot = true;
				}
				else if (UGridSlot* GSlot = Cast<UGridSlot>(Child->Slot))
				{
					FoundMaxRow = FMath::Max(FoundMaxRow, GSlot->GetRow());
					FoundMaxCol = FMath::Max(FoundMaxCol, GSlot->GetColumn());
					bFoundGridSlot = true;
				}
			}
		}
		else
		{
			// If we used panel settings, we still need to scan for Class Detection!
			const int32 ChildrenCount = IconContainer->GetChildrenCount();
			for (int32 i = 0; i < ChildrenCount; ++i)
			{
				UWidget* Child = IconContainer->GetChildAt(i);
				if (!Child) continue;
				
				if (!IconWidgetClass)
				{
					if (URTSUnitIconWidget* IconWidget = Cast<URTSUnitIconWidget>(Child))
					{
						IconWidgetClass = IconWidget->GetClass();
						break; 
					}
				}
			}
		}

		// Apply Detected Size
		if (bFoundGridSlot)
		{
			MaxRows = FoundMaxRow + 1; // 0-based index to Count
			MaxColumns = FoundMaxCol + 1;
			UE_LOG(LogTemp, Log, TEXT("RTSSelectionWidget: Detected Grid Logic (%s): %d Rows x %d Cols"), 
				bUsedPanelSettings ? TEXT("Explicit Panel Config") : TEXT("Child Placement"), 
				MaxRows, MaxColumns);
		}

		// Clear templates from view so we can populate fresh data
		IconContainer->ClearChildren();
	}

	if (IconWidgetClass)
	{
		UE_LOG(LogTemp, Log, TEXT("RTSSelectionWidget: IconWidgetClass resolved to %s"), *IconWidgetClass->GetName());

		// --- Initialize Widget Pool ---
		// Calculate Total Slots based on Grid Dimensions
		ItemsPerPage = MaxRows * MaxColumns;
		UE_LOG(LogTemp, Log, TEXT("RTSSelectionWidget: Initializing Pool for %d x %d = %d slots."), MaxRows, MaxColumns, ItemsPerPage);

		IconSlots.Reset();
		CountSlots.Reset();

		// Support for various container types
		UUniformGridPanel* UniformGrid = Cast<UUniformGridPanel>(IconContainer);
		UGridPanel* GenericGrid = Cast<UGridPanel>(IconContainer);
		UWrapBox* WrapBox = Cast<UWrapBox>(IconContainer);

		// Helper for layout
		int32 CurrentCol = 0;
		int32 CurrentRow = 0;
		auto AdvanceCursor = [&]() {
			CurrentCol++;
			if (CurrentCol >= MaxColumns)
			{
				CurrentCol = 0;
				CurrentRow++;
			}
		};

		// Create Fixed Pool (Icon Only Mode for now - Summary Count complicates fixed slots)
		// For simplicity, we create ItemsPerPage ICONS. 
		// If Summary mode needs CountText, we might dynamic spawn those or dual-pool.
		// Given user requirement "Fixed Array", let's spawn the Max capacity.

		for (int32 i = 0; i < ItemsPerPage; i++)
		{
			if (IconWidgetClass->IsChildOf(UUserWidget::StaticClass()))
			{
				URTSUnitIconWidget* NewWidget = CreateWidget<URTSUnitIconWidget>(this, IconWidgetClass);
				if (NewWidget)
				{
					// Add to Container
					if (UniformGrid)
					{
						UUniformGridSlot* NewSlot = UniformGrid->AddChildToUniformGrid(NewWidget, CurrentRow, CurrentCol);
						if (NewSlot) { NewSlot->SetHorizontalAlignment(HAlign_Fill); NewSlot->SetVerticalAlignment(VAlign_Fill); }
						AdvanceCursor();
					}
					else if (GenericGrid)
					{
						UGridSlot* NewSlot = GenericGrid->AddChildToGrid(NewWidget, CurrentRow, CurrentCol);
						if (NewSlot) { NewSlot->SetHorizontalAlignment(HAlign_Fill); NewSlot->SetVerticalAlignment(VAlign_Fill); }
						AdvanceCursor();
					}
					else if (WrapBox)
					{
						WrapBox->AddChildToWrapBox(NewWidget);
					}
					else
					{
						IconContainer->AddChild(NewWidget);
					}

					// Store in Pool and Hide
					NewWidget->SetVisibility(ESlateVisibility::Hidden); // Hidden preserves layout in Grid? No, Collapsed does? 
					// Actually, for UniformGrid, we want it to take space? 
					// If Hidden, specific implementations might vary, but usually Hidden = Takes Space, Invisible.
					// Collapsed = No Space.
					// User wants "Fixed Array", implies fixed layout structure. Hidden is correct.
					
					IconSlots.Add(NewWidget);
				}
			}
		}
		UE_LOG(LogTemp, Log, TEXT("RTSSelectionWidget: Initialized Pool with %d widgets."), IconSlots.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSSelectionWidget: IconWidgetClass is NULL! Grid will be empty. Set it in Details or add a template child."));
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.AddDynamic(this, &URTSSelectionWidget::OnSelectionUpdated);
			}
		}
	}
}

void URTSSelectionWidget::NativeDestruct()
{
// ...
}

// ...


void URTSSelectionWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	RefreshGrid(View);
}

void URTSSelectionWidget::RefreshGrid(const FRTSSelectionView& View)
{
	const TArray<FRTSUnitData>& AllItems = View.Items;
	ERTSSelectionMode Mode = View.Mode;
	FString ActiveKey = View.ActiveGroupKey;
	
	UE_LOG(LogTemp, Log, TEXT("RTSSelectionWidget::RefreshGrid - Mode: %d, Items: %d, ActiveKey: %s"), (int32)Mode, AllItems.Num(), *ActiveKey);

	// --- 1. Handle Panel Visibility (Single vs Grid) ---
	bool bShowDetail = (Mode == ERTSSelectionMode::Single && SingleUnitDetail != nullptr);

	if (bShowDetail)
	{
		// Show Detail, Hide Grid
		if (SingleUnitDetail) SingleUnitDetail->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (IconContainer) IconContainer->SetVisibility(ESlateVisibility::Collapsed);
		
		// Grid population is skipped because we are showing the detail view
		return;
	}
	else
	{
		// Show Grid, Hide Detail (or if Detail is null, we cleanup anyway)
		if (SingleUnitDetail) SingleUnitDetail->SetVisibility(ESlateVisibility::Collapsed);
		if (IconContainer) IconContainer->SetVisibility(ESlateVisibility::Visible);
	}

	// --- 3. Update Grid from Pool ---
	if (!IconContainer || IconSlots.Num() == 0)
	{
		// UE_LOG(LogTemp, Warning, TEXT("RTSSelectionWidget: internal pool empty or container missing."));
		return;
	}

	int32 StartIndex = 0;
	// We can only show as many items as we have slots
	int32 MaxVisible = FMath::Min(AllItems.Num(), IconSlots.Num());
	
	for (int32 i = 0; i < IconSlots.Num(); i++)
	{
		URTSUnitIconWidget* SlotWidget = IconSlots[i];
		if (!SlotWidget) continue;

		int32 DataIndex = StartIndex + i; // Simple linear mapping

		if (DataIndex < AllItems.Num())
		{
			// Valid Item
			const FRTSUnitData& Data = AllItems[DataIndex];
			
			// Update Data
			// Config: Show Icon, Show Bars
			SlotWidget->InitData(Data, true, true);
			
			// Highlight Logic
			bool bIsActive = ActiveKey.IsEmpty() || (Data.Name == ActiveKey);
			SlotWidget->SetIsActive(bIsActive);

			// Visible
			SlotWidget->SetVisibility(ESlateVisibility::Visible); // or SelfHitTestInvisible
		}
		else
		{
			// Empty Slot
			SlotWidget->SetVisibility(ESlateVisibility::Hidden); // Hidden = Layout Reserved. Collapsed = Gone.
		}
	}

	// Note: Summary Mode separate count text is temporarily disabled in Fixed Pool mode.
	// If you need counts, consider re-adding CountText to RTSUnitIconWidget or using an Overlay.

}
