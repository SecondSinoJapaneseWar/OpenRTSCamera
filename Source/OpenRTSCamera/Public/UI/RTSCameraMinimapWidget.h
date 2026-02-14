// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSCameraMinimapWidget.generated.h"

class URTSCamera;

/**
 * URTSCameraMinimapWidget
 * 
 * A widget that visualizes the RTS Camera's Field of View on a minimap.
 * It uses the Camera's BoundaryVolume to determine the coordinate system.
 * It handles input to move the camera (JumpTo).
 */
UCLASS(BlueprintType, Blueprintable)
class OPENRTSCAMERA_API URTSCameraMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	URTSCameraMinimapWidget(const FObjectInitializer& ObjectInitializer);

	/** 
	 * Initialize the controller. Tries to find the RTSCamera on the owning player's pawn/view target.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera|Minimap")
	void InitializeController();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float LineWidth = 2.0f;
	
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- Input Handling ---
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

private:
	void FindRTSCamera();

	/** Convert World Location (XY) to Widget Local Coordinates (UV * Size) */
	FVector2D ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const;

	/** Convert Widget Local Coordinates to World Location (XY) */
	FVector2D ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const;

protected:
	/** Cached reference to the RTSCamera */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "RTSCamera|Cache")
	TObjectPtr<URTSCamera> CachedRTSCamera;

	/** Cached reference to the actual Camera Component (Source of Truth for FOV) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "RTSCamera|Cache")
	TObjectPtr<UCameraComponent> CachedCameraComponent;

	/** Cached reference to the Spring Arm (Source of Truth for Zoom/Rotation) */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "RTSCamera|Cache")
	TObjectPtr<USpringArmComponent> CachedSpringArm;

	/** Derived from BoundaryVolume */
	FVector CachedBoundsOrigin = FVector::ZeroVector;
	FVector CachedBoundsExtent = FVector(100.f, 100.f, 100.f);
	bool bHasValidBounds = false;

	/** Input State */
	bool bIsDragging = false;
};
