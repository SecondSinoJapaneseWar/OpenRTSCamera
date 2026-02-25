#include "RTSHUD.h"
#include "RTSSelectionSubsystem.h"
#include "RTSSelectable.h"
#include "MassBattleFuncLib.h"
#include "RTSSelector.h"
#include "Engine/Canvas.h"
#include "LandmarkSubsystem.h"
#include "LandmarkTypes.h"
#include "Interfaces/RTSCommandInterface.h"
#include "Data/RTSCommandGridAsset.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"

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
	if (bIsPerformingSelection)
	{
		PerformSelection();
        bIsPerformingSelection = false; // CRITICAL: Reset the flag to stop continuous selection
	}

	// --- Landmark System Integration ---
	if (APlayerController* PC = GetOwningPlayerController())
	{
		if (UWorld* World = GetWorld())
		{
			if (ULandmarkSubsystem* LandmarkSys = World->GetSubsystem<ULandmarkSubsystem>())
			{
				// 1. Calculate Camera State
				FVector CamLoc;
				FRotator CamRot;
				PC->GetPlayerViewPoint(CamLoc, CamRot);

				// specific logic for OpenRTSCamera: Height is usually Z.
				const float MinHeight = 500.0f;
				const float MaxHeight = 10000.0f;
				float ZoomFactor = FMath::Clamp((CamLoc.Z - MinHeight) / (MaxHeight - MinHeight), 0.0f, 1.0f);

				// 2. Update Subsystem
				LandmarkSys->UpdateCameraState(CamLoc, CamRot, 90.0f, ZoomFactor);

				// 3. Delegate Drawing to Subsystem (It handles the HUD layer for landmarks)
                if (Canvas)
                {
                    LandmarkSys->DrawLandmarks(Canvas);
                }
			}
		}
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
	// 1. Prepare
	ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace;
    float DragDistSq = FVector2D::DistSquared(SelectionStart, SelectionEnd);

	URTSSelectionSubsystem* SelectionSubsystem = nullptr;
    URTSSelector* SelectorComponent = nullptr;
    APlayerController* PC = GetOwningPlayerController();
	
    if (PC)
	{
        SelectorComponent = PC->FindComponentByClass<URTSSelector>();
		if (const ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			SelectionSubsystem = LP->GetSubsystem<URTSSelectionSubsystem>();
		}

		if (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift))
		{
			Modifier = ERTSSelectionModifier::Add;
		}
	}

    TArray<AActor*> FinalActorSelection;
    TArray<FEntityHandle> FinalMassSelection;

    // 2. SEARCH (Direct & Concurrent)
    
    // A. Actor Path (The primary way to select anything, including Cities now)
    TArray<AActor*> RawActors;
    GetActorsInSelectionRectangle<AActor>(SelectionStart, SelectionEnd, RawActors, false, false);
    for (AActor* Actor : RawActors)
    {
        if (Actor && Actor->FindComponentByClass<URTSSelectable>())
        {
            FinalActorSelection.AddUnique(Actor);
        }
    }

    // B. Entity Path (Soldiers - Mass Battle Standard)
    PerformMassSelection(FinalMassSelection);

    // 3. APPLY
    if (SelectionSubsystem)
    {
        SelectionSubsystem->SetSelectedUnits(FinalActorSelection, FinalMassSelection, Modifier);
    }

	// 5. Toggle Logic (Shift + Single Click = Deselect)
	// ONLY apply toggle if this was a Click (not a Box Drag).
	// Threshold: MinSelectionSizeSq (Synced with Visuals).
	
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
		if (FinalActorSelection.Num() > 0)
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
	
	// Update Data Store - REMOVED DUPLICATE CALL
	// SetSelectedUnits was already called above after initial search.
	// Logic now relies on that single entry point.

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
	float MinX = FMath::Min(SelectionStart.X, SelectionEnd.X);
    float MinY = FMath::Min(SelectionStart.Y, SelectionEnd.Y);
    float MaxX = FMath::Max(SelectionStart.X, SelectionEnd.X);
    float MaxY = FMath::Max(SelectionStart.Y, SelectionEnd.Y);
    
    float Width = MaxX - MinX;
    float Height = MaxY - MinY;
    float DragDistSq = Width * Width + Height * Height;

    // --- 核心逻辑：统一平截头体选择 (Unified Frustum Selection) ---
    // 无论是点选还是框选，都使用 ViewTraceForAgents 进行后端处理
    bool bIsClick = (DragDistSq < MinSelectionSizeSq);
    
    if (bIsClick)
    {
        // 如果是点选，将单点向四周扩展 1 像素，形成一个微型 2x2 选区
        // 这样可以确保平截头体法线非零，且能利用 Mass 优化的过滤逻辑
        MinX -= 1.0f;
        MaxX += 1.0f;
        MinY -= 1.0f;
        MaxY += 1.0f;
    }

	// 关键：逆时针排列（左上→左下→右下→右上）确保视锥体平面法线朝内
	// 顺时针排列会使法线朝外，导致 PlaneDot 过滤掉框内所有实体
	TArray<FVector2D> ScreenPoints = {
		FVector2D(MinX, MinY),  // 左上
		FVector2D(MinX, MaxY),  // 左下
		FVector2D(MaxX, MaxY),  // 右下
		FVector2D(MaxX, MinY),  // 右上
	};

	FViewTracePoints TracePoints;
	TracePoints.ViewPoint = PC->PlayerCameraManager->GetCameraLocation();

	for (const FVector2D& ScreenPoint : ScreenPoints)
	{
		FVector WorldPos, WorldDirection;
		if (PC->DeprojectScreenPositionToWorld(ScreenPoint.X, ScreenPoint.Y, WorldPos, WorldDirection))
		{
			TracePoints.SelectionPoints.Add(WorldPos + WorldDirection * 100000.0f);
		}
	}

	if (TracePoints.SelectionPoints.Num() == 4)
	{
		bool bHit = false;
		TArray<FTraceResult> Results;
        
		int32 LocalKeepCount = bIsClick ? 1 : -1;
		ESortMode SortMode = bIsClick ? ESortMode::NearToFar : ESortMode::None;

#if WITH_EDITOR
		FTraceDrawDebugConfig DebugCfg;
		DebugCfg.bDrawDebugShape = true;
		DebugCfg.Duration = 2.0f;
		UMassBattleFuncLib::ViewTraceForAgents(this, bHit, Results, LocalKeepCount, TracePoints, false, FVector::ZeroVector, 1.0f, SortMode,
			FVector::ZeroVector, FEntityArray(), FMassBattleQuery(), DebugCfg);
#else
		UMassBattleFuncLib::ViewTraceForAgents(this, bHit, Results, LocalKeepCount, TracePoints, false, FVector::ZeroVector, 1.0f, SortMode);
#endif

		UE_LOG(LogTemp, Warning, TEXT("PerformMassSelection: bHit=%d Results=%d IsClick=%d"), bHit ? 1:0, Results.Num(), bIsClick?1:0);

		if (bHit)
		{
			for (const FTraceResult& Result : Results)
			{
				OutEntities.Add(Result.Entity);
			}
		}
	}
}
