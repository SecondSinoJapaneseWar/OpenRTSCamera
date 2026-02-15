#include "RTSHUD.h"
#include "RTSSelector.h"
#include "Engine/Canvas.h"

// Constructor implementation: Initializes default values.
ARTSHUD::ARTSHUD()
{
	SelectionBoxColor = FLinearColor::Green;
	SelectionBoxThickness = 1.0f;
	bIsDrawingSelectionBox = false;
	bIsPerformingSelection = false;
}

// Implementation of the DrawHUD function. It's called every frame to draw the HUD.
void ARTSHUD::DrawHUD()
{
	Super::DrawHUD(); // Call the base class implementation.

	// Draw the selection box if it's active.
	if (bIsDrawingSelectionBox)
	{
		DrawSelectionBox(SelectionStart, SelectionEnd);
	}

	// Perform selection actions if required.
	if (bIsPerformingSelection)
	{
		PerformSelection();
	}
}

// Starts the selection process, setting the initial point and activating the selection flag.
void ARTSHUD::BeginSelection(const FVector2D& StartPoint)
{
	SelectionStart = StartPoint;
	bIsDrawingSelectionBox = true;
}

// Updates the current endpoint of the selection box.
void ARTSHUD::UpdateSelection(const FVector2D& EndPoint)
{
	SelectionEnd = EndPoint;
}

// Ends the selection process and triggers the selection logic.
void ARTSHUD::EndSelection()
{
	bIsDrawingSelectionBox = false;
	bIsPerformingSelection = true;
}

// Default implementation of DrawSelectionBox. Draws a rectangle on the HUD.
void ARTSHUD::DrawSelectionBox_Implementation(const FVector2D& StartPoint, const FVector2D& EndPoint)
{
	if (Canvas)
	{
		// Calculate corners of the selection rectangle.
		const auto TopRight = FVector2D(SelectionEnd.X, SelectionStart.Y);
		const auto BottomLeft = FVector2D(SelectionStart.X, SelectionEnd.Y);

		// Draw lines to form the selection rectangle.
		Canvas->K2_DrawLine(SelectionStart, TopRight, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(TopRight, SelectionEnd, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(SelectionEnd, BottomLeft, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(BottomLeft, SelectionStart, SelectionBoxThickness, SelectionBoxColor);
	}
}

// Default implementation of PerformSelection. Selects actors within the selection box.
void ARTSHUD::PerformSelection_Implementation()
{
	// Array to store actors that are within the selection rectangle.
	TArray<AActor*> SelectedActors;
	GetActorsInSelectionRectangle<AActor>(SelectionStart, SelectionEnd, SelectedActors, false, false);

	// Filter for Selectable Actors
	TArray<AActor*> ValidSelectableActors;
	for (AActor* Actor : SelectedActors)
	{
		if (Actor && Actor->FindComponentByClass<URTSSelectable>())
		{
			ValidSelectableActors.Add(Actor);
		}
	}

	// Find the URTSSelector component and pass the selected actors to it.
	if (const auto PC = GetOwningPlayerController())
	{
		if (const auto SelectorComponent = PC->FindComponentByClass<URTSSelector>())
		{
			if (ValidSelectableActors.Num() > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("RTSHUD: Found %d Selectable Actors, skipping Mass selection."), ValidSelectableActors.Num());
				SelectorComponent->HandleSelectedActors(ValidSelectableActors);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("RTSHUD: No Selectable Actors found, attempting Mass selection."));
				// Clear previous selection and try MassBattle selection
				SelectorComponent->HandleSelectedActors(TArray<AActor*>());
				PerformMassSelection();
			}
		}
	}

	bIsPerformingSelection = false;
}

#include "MassBattleFuncLib.h"
#include "MassBattleStructs.h"

void ARTSHUD::PerformMassSelection()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;

	// Calculate selection box bounds
	FVector2D Min(FMath::Min(SelectionStart.X, SelectionEnd.X), FMath::Min(SelectionStart.Y, SelectionEnd.Y));
	FVector2D Max(FMath::Max(SelectionStart.X, SelectionEnd.X), FMath::Max(SelectionStart.Y, SelectionEnd.Y));

	// Minimum selection size threshold
	if (FVector2D::DistSquared(Min, Max) < 100.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSHUD: Selection box too small for Mass pick."));
		return;
	}

	TArray<FVector2D> ScreenPoints = {
		Min,
		FVector2D(Max.X, Min.Y),
		Max,
		FVector2D(Min.X, Max.Y)
	};

	FViewTracePoints TracePoints;
	TracePoints.ViewPoint = PC->PlayerCameraManager->GetCameraLocation();

	for (const FVector2D& ScreenPoint : ScreenPoints)
	{
		FVector WorldPos, WorldDirection;
		if (PC->DeprojectScreenPositionToWorld(ScreenPoint.X, ScreenPoint.Y, WorldPos, WorldDirection))
		{
			// Project points onto a plane at some distance to form the frustum
			// Increased to 100,000 to ensure it covers the ground from typical RTS camera heights
			TracePoints.SelectionPoints.Add(WorldPos + WorldDirection * 100000.0f);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("RTSHUD: Generated %d Selection Points for Mass Trace."), TracePoints.SelectionPoints.Num());

	if (TracePoints.SelectionPoints.Num() == 4)
	{
		bool bHit = false;
		TArray<FTraceResult> Results;
		UMassBattleFuncLib::ViewTraceForAgents(this, bHit, Results, -1, TracePoints);

		if (bHit)
		{
			UE_LOG(LogTemp, Log, TEXT("RTSHUD: Selected %d Mass Entities"), Results.Num());
			for (const FTraceResult& Result : Results)
			{
				UE_LOG(LogTemp, Log, TEXT("  - Entity Index: %d"), Result.Entity.Index);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("RTSHUD: Mass Trace returned NO hits (bHit=false)."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("RTSHUD: Failed to generate 4 world points for Mass Trace."));
	}
}
