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
	/// 初始化控件可见性，并默认设置绘制线宽
	this->SetVisibility(ESlateVisibility::Visible);
}

void URTSCameraMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	/// 锁定交互属性并执行初始控制器搜索
	this->SetVisibility(ESlateVisibility::Visible);
	this->SetIsFocusable(true);
	
	this->InitializeController();
}

void URTSCameraMinimapWidget::InitializeController()
{
	if (APlayerController* playerController = this->GetOwningPlayer())
	{
		playerController->bEnableClickEvents = true;
		playerController->bEnableMouseOverEvents = true;
	}
	this->findRTSCamera();
}

void URTSCameraMinimapWidget::findRTSCamera()
{
	/// 尝试从当前的观察目标或 Pawn 中定位 RTSCamera 组件
	if (!this->cachedRTSCamera)
	{
		APlayerController* playerController = this->GetOwningPlayer();
		if (playerController)
		{
			if (AActor* viewTarget = playerController->GetViewTarget())
			{
				this->cachedRTSCamera = viewTarget->FindComponentByClass<URTSCamera>();
			}
			if (!this->cachedRTSCamera)
			{
				if (APawn* pawn = playerController->GetPawn())
				{
					this->cachedRTSCamera = pawn->FindComponentByClass<URTSCamera>();
				}
			}
		}
	}

	/// 若成功捕获组件，则建立响应式订阅并缓存辅助引用
	if (this->cachedRTSCamera)
	{
		/// 绑定视野更新委托：由相机的“主动推送”驱动 UI 的“局部失效”
		this->cachedRTSCamera->onMinimapFrustumUpdated.RemoveAll(this);
		this->cachedRTSCamera->onMinimapFrustumUpdated.AddUObject(this, &URTSCameraMinimapWidget::handleMinimapFrustumUpdated);

		AActor* cameraOwner = this->cachedRTSCamera->GetOwner();
		if (cameraOwner)
		{
			if (!this->cachedCameraComponent) 
			{
				this->cachedCameraComponent = cameraOwner->FindComponentByClass<UCameraComponent>();
			}
			if (!this->cachedSpringArm) 
			{
				this->cachedSpringArm = cameraOwner->FindComponentByClass<USpringArmComponent>();
			}
		}

		/// 初始同步地图边界数据
		if (!this->bHasValidBounds)
		{
			if (AActor* boundsActor = this->cachedRTSCamera->getMovementBoundaryVolume())
			{
				FVector origin;
				FVector extent;
				boundsActor->GetActorBounds(false, origin, extent);
				this->cachedBoundsOrigin = origin;
				this->cachedBoundsExtent = extent;
				this->bHasValidBounds = true;
			}
		}
	}
}

void URTSCameraMinimapWidget::handleMinimapFrustumUpdated()
{
	/// 响应式重绘核心：仅在相机通过委托告知数据变动时，才标记 Slate 渲染层失效。
	/// 开发提示：配合 Invalidation Box 使用，可使本控件在静止状态下完全忽略 NativePaint 开销。
	this->Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D URTSCameraMinimapWidget::ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const
{
	/// 将世界坐标系下的点线性映射至小地图控件的局部 0-1 空间，并适配轴向偏移
	if (this->cachedBoundsExtent.X < KINDA_SMALL_NUMBER || this->cachedBoundsExtent.Y < KINDA_SMALL_NUMBER) 
	{
		return FVector2D::ZeroVector;
	}

	float normalizedX = (WorldPos.X - (this->cachedBoundsOrigin.X - this->cachedBoundsExtent.X)) / (2.0f * this->cachedBoundsExtent.X);
	float normalizedY = (WorldPos.Y - (this->cachedBoundsOrigin.Y - this->cachedBoundsExtent.Y)) / (2.0f * this->cachedBoundsExtent.Y);

	return FVector2D(normalizedY * WidgetSize.X, (1.0f - normalizedX) * WidgetSize.Y);
}

FVector2D URTSCameraMinimapWidget::ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const
{
	/// 将小地图局部像素坐标反投影回世界地图水平面的 X/Y 坐标
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f) 
	{
		return FVector2D::ZeroVector;
	}

	float uParam = LocalPos.X / WidgetSize.X;
	float vParam = LocalPos.Y / WidgetSize.Y;

	float normalizedX = 1.0f - vParam;
	float normalizedY = uParam;

	float worldX = (this->cachedBoundsOrigin.X - this->cachedBoundsExtent.X) + normalizedX * (2.0f * this->cachedBoundsExtent.X);
	float worldY = (this->cachedBoundsOrigin.Y - this->cachedBoundsExtent.Y) + normalizedY * (2.0f * this->cachedBoundsExtent.Y);

	return FVector2D(worldX, worldY);
}

int32 URTSCameraMinimapWidget::NativePaint(
	const FPaintArgs& Args, 
	const FGeometry& AllottedGeometry, 
	const FSlateRect& MyCullingRect, 
	FSlateWindowElementList& OutDrawElements, 
	int32 LayerId, 
	const FWidgetStyle& InWidgetStyle, 
	bool bParentEnabled
) const
{
	/// 执行基础绘制流程。注：如果当前组件没有被 Invalidate，Slate 可能会完全跳过此函数执行。
	int32 maxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!this->cachedRTSCamera || !this->bHasValidBounds)
	{
		const_cast<URTSCameraMinimapWidget*>(this)->findRTSCamera();
	}

	if (!this->cachedRTSCamera || !this->bHasValidBounds || !this->cachedSpringArm || !this->cachedCameraComponent)
	{
		return maxLayerId;
	}

	FVector2D geometrySize = AllottedGeometry.GetLocalSize();
	if (geometrySize.X < 1.0f || geometrySize.Y < 1.0f)
	{
		return maxLayerId;
	}

	/// 战术兼容性调整：虽然静态数组更直接，但由于 Slate 的 MakeLines API 严格要求 TArray 类型容器，
	/// 此处采用预分配空间的 TArray 以平衡性能与接口规范。
	TArray<FVector2D> drawPoints;
	drawPoints.Reserve(5);

	for (int32 i = 0; i < 4; ++i)
	{
		const FVector& worldPt = this->cachedRTSCamera->minimapFrustumPoints[i];
		drawPoints.Add(this->ConvertWorldToWidgetLocal(FVector2D(worldPt.X, worldPt.Y), geometrySize));
	}

	if (drawPoints.Num() > 0)
	{
		/// 彻底消除断言崩溃：显式拷贝首个元素至栈变量。
		/// UE 5.6 严禁直接 Add 容器内部的元素地址，以防扩容时发生非法访问。
		const FVector2D closedPoint = drawPoints[0];
		drawPoints.Add(closedPoint);
	}

	FSlateDrawElement::MakeLines(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(),
		drawPoints,
		ESlateDrawEffect::None,
		FLinearColor::White,
		true,
		this->lineWidth
	);

	return maxLayerId + 1;
}

FReply URTSCameraMinimapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	/// 响应点击：将屏幕点击直接转化为相机的战略突变
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		this->bIsDragging = true;
		if (this->cachedRTSCamera)
		{
			FVector2D localPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			FVector2D worldPos = this->ConvertWidgetLocalToWorld(localPos, InGeometry.GetLocalSize());
			this->cachedRTSCamera->jumpTo(FVector(worldPos.X, worldPos.Y, 0.0f));
		}
		return FReply::Handled().CaptureMouse(this->TakeWidget());
	}
	return FReply::Unhandled();
}

FReply URTSCameraMinimapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	/// 释放拖拽锁
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && this->bIsDragging)
	{
		this->bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply URTSCameraMinimapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	/// 拖拽追踪：连续更新相机位置
	if (this->bIsDragging && this->HasMouseCapture())
	{
		if (this->cachedRTSCamera)
		{
			FVector2D localPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			FVector2D worldPos = this->ConvertWidgetLocalToWorld(localPos, InGeometry.GetLocalSize());
			this->cachedRTSCamera->jumpTo(FVector(worldPos.X, worldPos.Y, 0.0f));
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void URTSCameraMinimapWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
}

void URTSCameraMinimapWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
}
