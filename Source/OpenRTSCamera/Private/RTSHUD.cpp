#include "RTSHUD.h"
#include "RTSSelectionSubsystem.h"
#include "RTSSelectable.h"
#include "MassBattleFuncLib.h"
#include "RTSSelector.h"
#include "Engine/Canvas.h"

// Constructor implementation: Initializes default values.
ARTSHUD::ARTSHUD()
{
	SelectionBoxColor = FLinearColor::Green;
	SelectionBoxFillColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.15f);
	SelectionBoxThickness = 1.0f;
	MinSelectionSizeSq = 1.0f; // 1 pixel threshold as requested
	bIsDrawingSelectionBox = false;
	bIsPerformingSelection = false;
}

// Implementation of the DrawHUD function. It's called every frame to draw the HUD.
void ARTSHUD::DrawHUD()
{
	Super::DrawHUD(); // Call the base class implementation.

	// Draw the selection box if it's active AND large enough to be a box.
	if (bIsDrawingSelectionBox)
	{
		if (FVector2D::DistSquared(SelectionStart, SelectionEnd) > MinSelectionSizeSq)
		{
			DrawSelectionBox(SelectionStart, SelectionEnd);
		}
	}

	// Perform selection actions if required.
	// Perform selection actions if required.
	if (bIsPerformingSelection)
	{
		PerformSelection();
	}

	// --- Input Polling (One-Step Solution) ---
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (PC->WasInputKeyJustPressed(EKeys::Tab))
		{
			if (const ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
				{
					Subsystem->CycleGroup();
				}
			}
		}
	}
}

// Starts the selection process, setting the initial point and activating the selection flag.
void ARTSHUD::BeginSelection(const FVector2D& StartPoint)
{
	SelectionStart = StartPoint;
	SelectionEnd = StartPoint; // Initialize End to Start to avoid stale data
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
		// Calculate Top-Left and Size
		float MinX = FMath::Min(SelectionStart.X, SelectionEnd.X);
		float MinY = FMath::Min(SelectionStart.Y, SelectionEnd.Y);
		float Width = FMath::Abs(SelectionEnd.X - SelectionStart.X);
		float Height = FMath::Abs(SelectionEnd.Y - SelectionStart.Y);

		// 1. Draw Fill (Semi-transparent)
		if (Width > 0 && Height > 0)
		{
			// Note: K2_DrawRect uses current Canvas position? No, it usually takes screen pos?
			// Actually K2_DrawRect is tricky in UCanvas. 
			// Standard way: Canvas->K2_DrawTexture(WhiteTexture, ScreenPos, ScreenSize, ... Tint).
			// If we don't have a WhiteTexture, Update: UCanvas::K2_DrawMaterial matches best?
			// Let's use `Canvas->K2_DrawPolygon`? No.
			
			// Ah, `DrawRect` (C++ API): `Canvas->DrawTile(WhiteTexture, X, Y, W, H, ...)`
			// Wait, let's look at `Canvas->K2_DrawRect`. It exists in `UCanvas`.
			// `void UCanvas::K2_DrawRect(FLinearColor RenderTextureColor, FVector2D ScreenPosition, FVector2D ScreenSize)`
			
			DrawRect(SelectionBoxFillColor, MinX, MinY, Width, Height);
		}

		// 2. Draw Borders
		const auto TopRight = FVector2D(SelectionEnd.X, SelectionStart.Y);
		const auto BottomLeft = FVector2D(SelectionStart.X, SelectionEnd.Y);

		Canvas->K2_DrawLine(SelectionStart, TopRight, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(TopRight, SelectionEnd, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(SelectionEnd, BottomLeft, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(BottomLeft, SelectionStart, SelectionBoxThickness, SelectionBoxColor);
	}
}

#include "RTSSelectionSubsystem.h"

// Default implementation of PerformSelection. Selects actors within the selection box.
void ARTSHUD::PerformSelection_Implementation()
{
	// 1. Determine Selection Constraints & Modifier
	bool bCanSelectActors = true;
	bool bCanSelectMass = true;
	ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace;

	URTSSelectionSubsystem* SelectionSubsystem = nullptr;
	URTSSelector* SelectorComponent = nullptr;

	APlayerController* PC = GetOwningPlayerController();
	if (PC)
	{
		// Find Components/Subsystems
		SelectorComponent = PC->FindComponentByClass<URTSSelector>();
		if (const ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			SelectionSubsystem = LP->GetSubsystem<URTSSelectionSubsystem>();
		}

		// Check Input Modifier (Shift = Add)
		if (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift))
		{
			Modifier = ERTSSelectionModifier::Add;

			// "Type Consistency" Logic:
			// If we are ADDING to selection, we restrict new selection to match existing type.
			if (SelectionSubsystem)
			{
				if (SelectionSubsystem->HasSelectedActors())
				{
					bCanSelectMass = false; // Already have Actors -> Don't add Mass
				}
				else if (SelectionSubsystem->HasSelectedMass())
				{
					bCanSelectActors = false; // Already have Mass -> Don't add Actors
				}
			}
		}
	}

	// 2. Perform Actor Selection (if allowed)
	TArray<AActor*> FinalActorSelection;
	
	if (bCanSelectActors)
	{
		TArray<AActor*> RawActors;
		GetActorsInSelectionRectangle<AActor>(SelectionStart, SelectionEnd, RawActors, false, false);

		// Filter for Selectable
		for (AActor* Actor : RawActors)
		{
			if (Actor && Actor->FindComponentByClass<URTSSelectable>())
			{
				FinalActorSelection.Add(Actor);
			}
		}
	}

	// 3. Perform Mass Selection (Fallback logic: Only if No Actors found AND Mass is allowed)
	TArray<FEntityHandle> FinalMassSelection;
	bool bActorsFound = FinalActorSelection.Num() > 0;

	if (!bActorsFound && bCanSelectMass)
	{
		// Only attempt Mass select if we didn't Pick Actors (Priority) AND Mass is allowed (Consistency)
		PerformMassSelection(FinalMassSelection);
	}

	// 4. Toggle Logic (Shift + Single Click = Deselect)
	// 4. Toggle Logic (Shift + Single Click = Deselect)
	// ONLY apply toggle if this was a Click (not a Box Drag).
	// Threshold: MinSelectionSizeSq (Synced with Visuals).
	float DragDistSq = FVector2D::DistSquared(SelectionStart, SelectionEnd);
	
	if (Modifier == ERTSSelectionModifier::Add && SelectionSubsystem)
	{
		UE_LOG(LogTemp, Log, TEXT("RTSHUD: Shift Action - DragDistSq: %f (Threshold: %f)"), DragDistSq, MinSelectionSizeSq);
		
		if (DragDistSq <= MinSelectionSizeSq)
		{
			// Case A: Single Actor Toggle
			if (FinalActorSelection.Num() == 1 && FinalMassSelection.Num() == 0)
			{
				if (SelectionSubsystem->IsActorSelected(FinalActorSelection[0]))
				{
					Modifier = ERTSSelectionModifier::Remove;
					UE_LOG(LogTemp, Log, TEXT("RTSHUD: Toggling Single Actor OFF (Remove)."));
				}
			}
			// Case B: Single Mass Entity Toggle
			else if (FinalActorSelection.Num() == 0 && FinalMassSelection.Num() == 1)
			{
				if (SelectionSubsystem->IsEntitySelected(FinalMassSelection[0]))
				{
					Modifier = ERTSSelectionModifier::Remove;
					UE_LOG(LogTemp, Log, TEXT("RTSHUD: Toggling Single Entity OFF (Remove)."));
				}
			}
		}
	}

	// 5. Ctrl + Click (Select All of Same Type On Screen)
	if (PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)))
	{
		// Only apply if it was a Click (not a Box Drag)
		if (DragDistSq <= MinSelectionSizeSq)
		{
			// Strategy: If we clicked a single unit, find all matching units on screen.
			
			// 1. Actor Group Selection
			if (FinalActorSelection.Num() == 1)
			{
				AActor* TemplateActor = FinalActorSelection[0];
				if (TemplateActor)
				{
					UClass* MatchClass = TemplateActor->GetClass();
					
					// Get Viewport Size
					int32 ViewportX, ViewportY;
					PC->GetViewportSize(ViewportX, ViewportY);
					
					// Select All in Viewport
					TArray<AActor*> AllScreenActors;
					GetActorsInSelectionRectangle<AActor>(FVector2D(0,0), FVector2D(ViewportX, ViewportY), AllScreenActors, false, false);
					
					// Filter by Class
					FinalActorSelection.Reset();
					for(AActor* Act : AllScreenActors)
					{
						if (Act && Act->GetClass() == MatchClass && Act->FindComponentByClass<URTSSelectable>())
						{
							FinalActorSelection.Add(Act);
						}
					}
					
					// Force Replace Mode for Group Select
					Modifier = ERTSSelectionModifier::Replace;
					// Clear Mass (prioritize Actor group)
					FinalMassSelection.Reset();
				}
			}
			// 2. Mass Entity Group Selection (Future TODO if specific Mass types needed)
			else if (FinalMassSelection.Num() > 0)
			{
				// For Mass, we might need to check Archetype or Entity Config traits.
				// For now, if we Ctrl+Click a Mass unit, we might select ALL Mass units on screen?
				// Simplistic implementation: Select ALL valid Mass implementation if we clicked one.
				
				// Re-run PerformMassSelection with Full Screen?
				// Note: Mass selection is expensive. Let's stick to Actor logic first as requested.
			}
		}
	}

	// 6. Update Visuals & Subsystem
	
	// Visual Highlighting (Actors)
	if (SelectorComponent)
	{
		if (bActorsFound)
		{
			UE_LOG(LogTemp, Log, TEXT("RTSHUD: Found %d Selectable Actors."), FinalActorSelection.Num());
			SelectorComponent->HandleSelectedActors(FinalActorSelection);
		}
		else
		{
			// Clear Actor visuals (we either found nothing or found Mass)
			SelectorComponent->HandleSelectedActors(TArray<AActor*>());
			
			if (FinalMassSelection.Num() > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("RTSHUD: Selected %d Mass Entities."), FinalMassSelection.Num());
			}
		}
	}
	
	// Update Data Store
	if (SelectionSubsystem)
	{
		SelectionSubsystem->SetSelectedUnits(FinalActorSelection, FinalMassSelection, Modifier);
	}

	bIsPerformingSelection = false;
}

#include "MassBattleFuncLib.h"
#include "MassBattleStructs.h"

void ARTSHUD::PerformMassSelection(TArray<FEntityHandle>& OutEntities)
{
	OutEntities.Reset();
	
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
				OutEntities.Add(Result.Entity);
				// Log abbreviated
				// UE_LOG(LogTemp, Log, TEXT("  - Entity Index: %d"), Result.Entity.Index);
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
