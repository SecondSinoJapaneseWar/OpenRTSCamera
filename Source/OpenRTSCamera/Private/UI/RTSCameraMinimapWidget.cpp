// Copyright 2024 Jesus Bracho All Rights Reserved.

#include "UI/RTSCameraMinimapWidget.h"
#include "RTSCamera.h"
#include "OpenRTSCamera.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"



URTSCameraMinimapWidget::URTSCameraMinimapWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::Visible);
}

void URTSCameraMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	// Force visibility and interaction flags at runtime to override BP defaults
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
	
	InitializeController();
}

void URTSCameraMinimapWidget::InitializeController()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bEnableClickEvents = true;
		PC->bEnableMouseOverEvents = true;
	}
	FindRTSCamera();
}

void URTSCameraMinimapWidget::FindRTSCamera()
{
	// 1. Try ViewTarget 
	if (!CachedRTSCamera)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			if (AActor* ViewTarget = PC->GetViewTarget())
			{
				CachedRTSCamera = ViewTarget->FindComponentByClass<URTSCamera>();
			}
			if (!CachedRTSCamera)
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					CachedRTSCamera = Pawn->FindComponentByClass<URTSCamera>();
				}
			}
		}
	}

	// 3. Cache Dependencies
	if (CachedRTSCamera)
	{
		AActor* Owner = CachedRTSCamera->GetOwner();
		if (Owner)
		{
			if (!CachedCameraComponent) CachedCameraComponent = Owner->FindComponentByClass<UCameraComponent>();
			if (!CachedSpringArm) CachedSpringArm = Owner->FindComponentByClass<USpringArmComponent>();
		}

		// Update Bounds (Retry if invalid)
		if (!bHasValidBounds)
		{
			if (AActor* BoundsActor = CachedRTSCamera->GetBoundaryVolume())
			{
				FVector Origin, Extent;
				BoundsActor->GetActorBounds(false, Origin, Extent);
				CachedBoundsOrigin = Origin;
				CachedBoundsExtent = Extent;
				bHasValidBounds = true;
				
				UE_LOG(LogOpenRTSCamera, Log, TEXT("RTSCameraMinimapWidget: Bounds Found! Origin=%s, Extent=%s"), *Origin.ToString(), *Extent.ToString());
			}
			else
			{
				static bool bLogOnce = false;
				if (!bLogOnce)
				{
					UE_LOG(LogOpenRTSCamera, Warning, TEXT("RTSCameraMinimapWidget: RTSCamera found but BoundaryVolume is missing!"));
					bLogOnce = true;
				}
			}
		}
	}
}

FVector2D URTSCameraMinimapWidget::ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const
{
	if (CachedBoundsExtent.X < KINDA_SMALL_NUMBER || CachedBoundsExtent.Y < KINDA_SMALL_NUMBER) return FVector2D::ZeroVector;

	// Normalized X/Y in Bounds Space
	float NormX = (WorldPos.X - (CachedBoundsOrigin.X - CachedBoundsExtent.X)) / (2.0f * CachedBoundsExtent.X);
	float NormY = (WorldPos.Y - (CachedBoundsOrigin.Y - CachedBoundsExtent.Y)) / (2.0f * CachedBoundsExtent.Y);

	// Map to Widget Space: World +X (North) -> Widget -Y, World +Y (East) -> Widget +X
	return FVector2D(NormY * WidgetSize.X, (1.0f - NormX) * WidgetSize.Y);
}

FVector2D URTSCameraMinimapWidget::ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const
{
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f) return FVector2D::ZeroVector;

	float U = LocalPos.X / WidgetSize.X;
	float V = LocalPos.Y / WidgetSize.Y;

	// Invert Coordinate Mapping:
	// U = NormY => NormY = U
	// V = 1.0 - NormX => NormX = 1.0 - V
	float NormX = 1.0f - V;
	float NormY = U;

	float WorldX = (CachedBoundsOrigin.X - CachedBoundsExtent.X) + NormX * (2.0f * CachedBoundsExtent.X);
	float WorldY = (CachedBoundsOrigin.Y - CachedBoundsExtent.Y) + NormY * (2.0f * CachedBoundsExtent.Y);

	return FVector2D(WorldX, WorldY);
}

int32 URTSCameraMinimapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 1. Ensure Camera Reference
	int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!CachedRTSCamera || !bHasValidBounds)
	{
		const_cast<URTSCameraMinimapWidget*>(this)->FindRTSCamera();
	}

	if (!CachedRTSCamera || !bHasValidBounds || !CachedSpringArm || !CachedCameraComponent)
	{
		return MaxLayerId;
	}

	// 2. Read Cached Data (Source of Truth: RTSCamera)
	const TArray<FVector>& WorldPoints = CachedRTSCamera->MinimapFrustumPoints;
	
	// Debug Log
	UE_LOG(LogOpenRTSCamera, Verbose, TEXT("MinimapWidget: CachedRTSCamera found. Points Num: %d"), WorldPoints.Num());

	if (WorldPoints.Num() < 4) 
	{
		UE_LOG(LogOpenRTSCamera, Warning, TEXT("MinimapWidget: Not enough points (%d)"), WorldPoints.Num());
		return MaxLayerId;
	}
	
	// 3. Convert & Draw
	FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	// Debug Widget Size to ensure it's not zero (layout issue)
	if (LocalSize.X < 1.0f || LocalSize.Y < 1.0f)
	{
		UE_LOG(LogOpenRTSCamera, Warning, TEXT("RTSCameraMinimapWidget: LocalSize is too small! (%s). Check UMG layout (Wrap with SizeBox?)."), *LocalSize.ToString());
	}
	else
	{
		// UE_LOG(LogOpenRTSCamera, Verbose, TEXT("RTSCameraMinimapWidget: LocalSize %s"), *LocalSize.ToString());
	}

	TArray<FVector2D> Points;
	
	for (const FVector& WorldPt : WorldPoints)
	{
		FVector2D ScreenPt = ConvertWorldToWidgetLocal(FVector2D(WorldPt), LocalSize);
		Points.Add(ScreenPt);
		// UE_LOG(LogOpenRTSCamera, Verbose, TEXT("  Pt: %s -> Screen: %s"), *WorldPt.ToString(), *ScreenPt.ToString());
	}

	// 4. Draw
	if (Points.Num() > 0)
	{
		const FVector2D FirstPoint = Points[0];
		Points.Add(FirstPoint); // Close Loop (Copy first to avoid reallocation crash)
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		FLinearColor::White,
		true,
		LineWidth
	);

	return MaxLayerId + 1;
}

FReply URTSCameraMinimapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	UE_LOG(LogOpenRTSCamera, Log, TEXT("MinimapWidget: OnMouseButtonDown (Button: %s)"), *InMouseEvent.GetEffectingButton().ToString());
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		if (CachedRTSCamera)
		{
			FVector2D ScreenPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			FVector2D WorldPos = ConvertWidgetLocalToWorld(
				ScreenPos, 
				InGeometry.GetLocalSize()
			);
			
			UE_LOG(LogOpenRTSCamera, Log, TEXT("Minimap Click: Screen=%s, World=%s. Jumping!"), *ScreenPos.ToString(), *WorldPos.ToString());

			// Maintain current Z, don't force 0 if possible, or use JumpTo which might handle it.
			// JumpTo set Z=0 in current impl. Let's trust JumpTo for now but log it.
			CachedRTSCamera->JumpTo(FVector(WorldPos, 0.0f));
		}
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply URTSCameraMinimapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply URTSCameraMinimapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging && HasMouseCapture())
	{
		if (CachedRTSCamera)
		{
			FVector2D WorldPos = ConvertWidgetLocalToWorld(
				InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), 
				InGeometry.GetLocalSize()
			);
			CachedRTSCamera->JumpTo(FVector(WorldPos, 0.0f));
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void URTSCameraMinimapWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	UE_LOG(LogOpenRTSCamera, Log, TEXT("MinimapWidget: Mouse Entered Widget Area"));
}

void URTSCameraMinimapWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	UE_LOG(LogOpenRTSCamera, Log, TEXT("MinimapWidget: Mouse Left Widget Area"));
}
