// Copyright 2024 Jesus Bracho All Rights Reserved.

#include "RTSCamera.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Runtime/CoreUObject/Public/UObject/ConstructorHelpers.h"

URTSCamera::URTSCamera()
{
	/// 设置组件基本生存期属性
	PrimaryComponentTick.bCanEverTick = true;
	this->cameraBlockingVolumeTag = FName("OpenRTSCamera#CameraBounds");
	this->collisionChannel = ECC_WorldStatic;
	this->dragExtent = 0.6f;
	this->distanceFromEdgeThreshold = 0.1f;
	this->enableCameraLag = true;
	this->enableCameraRotationLag = true;
	this->enableDynamicCameraHeight = true;
	this->enableEdgeScrolling = true;
	this->findGroundTraceLength = 100000;
	this->maximumZoomLength = 5000;
	this->minimumZoomLength = 500;
	this->maxMovementSpeed = 1024.0f;
	this->minMovementSpeed = 128.0f;
	this->currentMovementSpeed = this->minMovementSpeed; 
	this->rotationSpeed = 45;
	this->startingPitchAngle = -45.0f;
	this->startingYawAngle = 0;
	this->zoomCatchupSpeed = 4;
	this->zoomSpeed = -200;

	/// 载入并关联输入资产
	static ConstructorHelpers::FObjectFinder<UInputAction> xMoveActionFinder(TEXT("/OpenRTSCamera/Inputs/MoveCameraXAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> yMoveActionFinder(TEXT("/OpenRTSCamera/Inputs/MoveCameraYAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> rotateActionFinder(TEXT("/OpenRTSCamera/Inputs/RotateCameraAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> leftTurnActionFinder(TEXT("/OpenRTSCamera/Inputs/TurnCameraLeft"));
	static ConstructorHelpers::FObjectFinder<UInputAction> rightTurnActionFinder(TEXT("/OpenRTSCamera/Inputs/TurnCameraRight"));
	static ConstructorHelpers::FObjectFinder<UInputAction> zoomActionFinder(TEXT("/OpenRTSCamera/Inputs/ZoomCamera"));
	static ConstructorHelpers::FObjectFinder<UInputAction> dragActionFinder(TEXT("/OpenRTSCamera/Inputs/DragCamera"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> contextFinder(TEXT("/OpenRTSCamera/Inputs/OpenRTSCameraInputs"));

	this->moveCameraXAxisAction = xMoveActionFinder.Object;
	this->moveCameraYAxisAction = yMoveActionFinder.Object;
	this->rotateCameraAxisAction = rotateActionFinder.Object;
	this->turnCameraLeftAction = leftTurnActionFinder.Object;
	this->turnCameraRightAction = rightTurnActionFinder.Object;
	this->dragCameraAction = dragActionFinder.Object;
	this->zoomCameraAction = zoomActionFinder.Object;
	this->inputMappingContext = contextFinder.Object;
}

void URTSCamera::BeginPlay()
{
	Super::BeginPlay();

	const auto netMode = this->GetNetMode();
	if (netMode != NM_DedicatedServer)
	{
		/// 初始化依赖架构，建立输入映射链条
		this->resolveComponentDependencyPointers();
		this->setupInitialSpringArmState();
		this->locateMapBoundaryVolumeByTag();
		this->configureInputModeForEdgeScrolling();
		this->validateEnhancedInputAvailability();
		this->registerInputMappingContext();
		this->bindActionCallbacks();
	}
}

void URTSCamera::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const auto netMode = this->GetNetMode();
	
	/// 仅在大客户端中处理相机位置插值与状态同步
	if (netMode != NM_DedicatedServer && this->realTimeStrategyPlayerController->GetViewTarget() == this->cameraOwner)
	{
		this->deltaSeconds = DeltaTime;
		this->applyAccumulatedMovementCommands();
		this->executeEdgeScrollingEvaluation();
		this->rectifyRootHeightFromTerrain();
		this->handleTargetArmLengthInterpolation();
		this->updateFollowPositionIfTargetActive();
		this->enforceCameraMovementBounds();
	}
}

void URTSCamera::followTarget(AActor* target)
{
	/// 设置物理跟随标记，相机将进入逐帧对齐模式
	this->activeCameraFollowTarget = target;
}

void URTSCamera::unFollowTarget()
{
	/// 解除当前的相机跟随关系
	this->activeCameraFollowTarget = nullptr;
}

void URTSCamera::onZoomCameraActionTriggered(const FInputActionValue& value)
{
	/// 更新意图缩放距离并通过该参数联动调整移动阻尼感
	this->desiredZoomLength = FMath::Clamp(
		this->desiredZoomLength + value.Get<float>() * this->zoomSpeed,
		this->minimumZoomLength,
		this->maximumZoomLength
	);

	float speedAlpha = (this->desiredZoomLength - this->minimumZoomLength) / (this->maximumZoomLength - this->minimumZoomLength);
	this->currentMovementSpeed = FMath::Lerp(this->minMovementSpeed, this->maxMovementSpeed, speedAlpha);

	/// 缩放意图发生的瞬间即时投射視野框，确保 UI 的战略响应无延迟
	this->updateMinimapFrustum();
}

void URTSCamera::onRotateCameraActionTriggered(const FInputActionValue& value)
{
	/// 处理水平偏航角输入并更新世界变换
	const auto actorRotation = this->rootComponent->GetComponentRotation();
	this->rootComponent->SetWorldRotation(
		FRotator::MakeFromEuler(
			FVector(
				actorRotation.Euler().X,
				actorRotation.Euler().Y,
				actorRotation.Euler().Z + value.Get<float>()
			)
		)
	);
	this->updateMinimapFrustum();
}

void URTSCamera::onTurnCameraLeftActionTriggered(const FInputActionValue&)
{
	/// 向左执行定量的步进式偏转
	const auto relativeRot = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRot.Euler().X,
				relativeRot.Euler().Y,
				relativeRot.Euler().Z - this->rotationSpeed
			)
		)
	);
	this->updateMinimapFrustum();
}

void URTSCamera::onTurnCameraRightActionTriggered(const FInputActionValue&)
{
	/// 向右执行定量的步进式偏转
	const auto relativeRot = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRot.Euler().X,
				relativeRot.Euler().Y,
				relativeRot.Euler().Z + this->rotationSpeed
			)
		)
	);
	this->updateMinimapFrustum();
}

void URTSCamera::onMoveCameraYAxisActionTriggered(const FInputActionValue& value)
{
	/// 处理纵向平移请求
	this->requestCameraMovement(
		this->springArmComponent->GetForwardVector().X,
		this->springArmComponent->GetForwardVector().Y,
		value.Get<float>()
	);
}

void URTSCamera::onMoveCameraXAxisActionTriggered(const FInputActionValue& value)
{
	/// 处理横向平移请求
	this->requestCameraMovement(
		this->springArmComponent->GetRightVector().X,
		this->springArmComponent->GetRightVector().Y,
		value.Get<float>()
	);
}

void URTSCamera::onDragCameraActionTriggered(const FInputActionValue& value)
{
	/// 记录拖拽起始状态
	if (!this->isDragging && value.Get<bool>())
	{
		this->isDragging = true;
		this->dragInteractionInitialLocation = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	}

	/// 在激活期间计算平滑增量并转换为运动指令
	else if (this->isDragging && value.Get<bool>())
	{
		const auto mousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
		auto viewportSizeExtent = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
		viewportSizeExtent *= dragExtent;

		auto dragDelta = mousePosition - this->dragInteractionInitialLocation;
		dragDelta.X = FMath::Clamp(dragDelta.X, -viewportSizeExtent.X, viewportSizeExtent.X) / viewportSizeExtent.X;
		dragDelta.Y = FMath::Clamp(dragDelta.Y, -viewportSizeExtent.Y, viewportSizeExtent.Y) / viewportSizeExtent.Y;

		this->requestCameraMovement(
			this->springArmComponent->GetRightVector().X,
			this->springArmComponent->GetRightVector().Y,
			dragDelta.X
		);

		this->requestCameraMovement(
			this->springArmComponent->GetForwardVector().X,
			this->springArmComponent->GetForwardVector().Y,
			dragDelta.Y * -1
		);
	}

	/// 清理拖拽标记
	else if (this->isDragging && !value.Get<bool>())
	{
		this->isDragging = false;
	}
}

void URTSCamera::requestCameraMovement(const float x, const float y, const float scale)
{
	/// 将运动请求压入队列，供 Tick 阶段统一消化
	FMoveCameraCommand movementCmd;
	movementCmd.x = x;
	movementCmd.y = y;
	movementCmd.scale = scale;
	this->pendingMovementCommands.Push(movementCmd);
}

void URTSCamera::applyAccumulatedMovementCommands()
{
	/// 执行帧内所有挂起的平移指令并清空
	for (const auto& [x, y, scale] : this->pendingMovementCommands)
	{
		auto directionVector = FVector2D(x, y);
		directionVector.Normalize();
		directionVector *= this->currentMovementSpeed * scale * this->deltaSeconds;
		
		this->jumpTo(
			this->rootComponent->GetComponentLocation() + FVector(directionVector.X, directionVector.Y, 0.0f)
		);
	}

	this->pendingMovementCommands.Empty();
}

void URTSCamera::resolveComponentDependencyPointers()
{
	/// 获取所有关键组件的指针，作为该插件架构的基石
	this->cameraOwner = this->GetOwner();
	this->rootComponent = this->cameraOwner->GetRootComponent();
	this->cameraComponent = Cast<UCameraComponent>(this->cameraOwner->GetComponentByClass(UCameraComponent::StaticClass()));
	this->springArmComponent = Cast<USpringArmComponent>(this->cameraOwner->GetComponentByClass(USpringArmComponent::StaticClass()));
	this->realTimeStrategyPlayerController = UGameplayStatics::GetPlayerController(this->GetWorld(), 0);
}

void URTSCamera::setupInitialSpringArmState()
{
	/// 配置弹簧臂的物理约束与渲染初始位姿
	this->desiredZoomLength = this->minimumZoomLength;
	this->springArmComponent->TargetArmLength = this->desiredZoomLength;
	this->springArmComponent->bDoCollisionTest = false;
	this->springArmComponent->bEnableCameraLag = this->enableCameraLag;
	this->springArmComponent->bEnableCameraRotationLag = this->enableCameraRotationLag;
	this->springArmComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				0.0,
				this->startingPitchAngle,
				this->startingYawAngle
			)
		)
	);
}

void URTSCamera::locateMapBoundaryVolumeByTag()
{
	/// 在世界中通过静态标签检索用于运动边界约束的 Actor
	TArray<AActor*> results;
	UGameplayStatics::GetAllActorsOfClassWithTag(
		this->GetWorld(),
		AActor::StaticClass(),
		this->cameraBlockingVolumeTag,
		results
	);

	if (results.Num() > 0)
	{
		this->movementBoundaryVolume = results[0];
		/// 初始化視野投影静态缓存点并执行初次同步
		this->updateMinimapFrustum();
	}
}

void URTSCamera::configureInputModeForEdgeScrolling()
{
	/// 当鼠标用于边缘滚动时，强制应用视口锁定策略
	if (this->enableEdgeScrolling)
	{
		FInputModeGameAndUI gameModeSettings;
		gameModeSettings.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		gameModeSettings.SetHideCursorDuringCapture(false);
		this->realTimeStrategyPlayerController->SetInputMode(gameModeSettings);
	}
}

void URTSCamera::validateEnhancedInputAvailability()
{
	/// 校验当前的 InputComponent 是否兼容 Enhanced Input 语法
	if (Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent) == nullptr)
	{
		UKismetSystemLibrary::PrintString(
			this->GetWorld(),
			TEXT("Warning: RTSCamera requires Enhanced Input Component! Check Project Settings."), true, true,
			FLinearColor::Red,
			100
		);
	}
}

void URTSCamera::registerInputMappingContext()
{
	/// 向玩家输入子系统注册相机专用的映射上下文
	if (this->realTimeStrategyPlayerController && this->realTimeStrategyPlayerController->GetLocalPlayer())
	{
		if (const auto inputSystem = this->realTimeStrategyPlayerController->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			this->realTimeStrategyPlayerController->bShowMouseCursor = true;

			if (!inputSystem->HasMappingContext(this->inputMappingContext))
			{
				inputSystem->AddMappingContext(this->inputMappingContext, 0);
			}
		}
	}
}

void URTSCamera::bindActionCallbacks()
{
	/// 执行运动指令与 C++ 响应函数的逻辑挂挂接
	if (const auto eic = Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent))
	{
		eic->BindAction(this->zoomCameraAction, ETriggerEvent::Triggered, this, &URTSCamera::onZoomCameraActionTriggered);
		eic->BindAction(this->rotateCameraAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onRotateCameraActionTriggered);
		eic->BindAction(this->turnCameraLeftAction, ETriggerEvent::Triggered, this, &URTSCamera::onTurnCameraLeftActionTriggered);
		eic->BindAction(this->turnCameraRightAction, ETriggerEvent::Triggered, this, &URTSCamera::onTurnCameraRightActionTriggered);
		eic->BindAction(this->moveCameraXAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraXAxisActionTriggered);
		eic->BindAction(this->moveCameraYAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraYAxisActionTriggered);
		eic->BindAction(this->dragCameraAction, ETriggerEvent::Triggered, this, &URTSCamera::onDragCameraActionTriggered);
	}
}

void URTSCamera::setActiveCamera()
{
	/// 将玩家当前的渲染视角强制聚焦于此组件
	this->realTimeStrategyPlayerController->SetViewTarget(this->GetOwner());
}

void URTSCamera::jumpTo(const FVector position)
{
	/// 执行瞬时的视变换同步，并触发视野投影点手动刷新
	float cachedZ = this->rootComponent->GetComponentLocation().Z;
	this->rootComponent->SetWorldLocation(FVector(position.X, position.Y, cachedZ));
	this->updateMinimapFrustum();
}

void URTSCamera::executeEdgeScrollingEvaluation()
{
	/// 仅在功能开启且未进行拖拽干扰时，执行屏幕边缘检测
	if (this->enableEdgeScrolling && !this->isDragging)
	{
		const FVector locationBeforePush = this->rootComponent->GetComponentLocation();
		
		this->performEdgeScrollLeft();
		this->performEdgeScrollRight();
		this->performEdgeScrollUp();
		this->performEdgeScrollDown();

		if (!this->rootComponent->GetComponentLocation().Equals(locationBeforePush, 0.1f))
		{
			this->updateMinimapFrustum();
		}
	}
}

void URTSCamera::performEdgeScrollLeft()
{
	/// 基于鼠标左偏移计算平移推力
	const auto mp = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto vs = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedVal = 1 - UKismetMathLibrary::NormalizeToRange(mp.X, 0.0f, vs.X * this->distanceFromEdgeThreshold);

	const float alpha = UKismetMathLibrary::FClamp(normalizedVal, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(-1 * this->rootComponent->GetRightVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollRight()
{
	/// 基于鼠标右偏移计算平移推力
	const auto mp = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto vs = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedVal = UKismetMathLibrary::NormalizeToRange(mp.X, vs.X * (1 - this->distanceFromEdgeThreshold), vs.X);

	const float alpha = UKismetMathLibrary::FClamp(normalizedVal, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(this->rootComponent->GetRightVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollUp()
{
	/// 基于鼠标上偏移计算平移推力
	const auto mp = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto vs = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedVal = UKismetMathLibrary::NormalizeToRange(mp.Y, 0.0f, vs.Y * this->distanceFromEdgeThreshold);

	const float alpha = 1 - UKismetMathLibrary::FClamp(normalizedVal, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(this->rootComponent->GetForwardVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollDown()
{
	/// 基于鼠标下偏移计算平移推力
	const auto mp = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto vs = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedVal = UKismetMathLibrary::NormalizeToRange(mp.Y, vs.Y * (1 - this->distanceFromEdgeThreshold), vs.Y);

	const float alpha = UKismetMathLibrary::FClamp(normalizedVal, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(-1 * this->rootComponent->GetForwardVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::updateFollowPositionIfTargetActive()
{
	/// 将相机根坐标强行锚定在追随目标之上
	if (this->activeCameraFollowTarget != nullptr)
	{
		this->jumpTo(this->activeCameraFollowTarget->GetActorLocation());
	}
}

void URTSCamera::handleTargetArmLengthInterpolation()
{
	/// 基于 deltaSeconds 驱动缩放插值，完善物理层的顺滑过渡
	this->springArmComponent->TargetArmLength = FMath::FInterpTo(
		this->springArmComponent->TargetArmLength,
		this->desiredZoomLength,
		this->deltaSeconds,
		this->zoomCatchupSpeed
	);
}

void URTSCamera::rectifyRootHeightFromTerrain()
{
	/// 射线补偿检测：使根节点坐标实时贴合地形海拔
	if (this->enableDynamicCameraHeight)
	{
		const FVector currentRootXYZ = this->rootComponent->GetComponentLocation();
		const TArray<AActor*> excluded;

		FHitResult floorHit;
		bool bValidFloor = UKismetSystemLibrary::LineTraceSingle(
			this->GetWorld(),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z + this->findGroundTraceLength),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z - this->findGroundTraceLength),
			UEngineTypes::ConvertToTraceType(this->collisionChannel),
			true,
			excluded,
			EDrawDebugTrace::None,
			floorHit,
			true
		);

		if (bValidFloor)
		{
			this->rootComponent->SetWorldLocation(FVector(floorHit.Location.X, floorHit.Location.Y, floorHit.Location.Z));
		}
	}
}

void URTSCamera::enforceCameraMovementBounds()
{
	/// 强制检查并将相机坐标拉回预定义的体积包围阵列内
	if (this->movementBoundaryVolume != nullptr)
	{
		const FVector posToClamp = this->rootComponent->GetComponentLocation();
		FVector boxOrigin;
		FVector boxExtents;
		this->movementBoundaryVolume->GetActorBounds(false, boxOrigin, boxExtents);

		this->rootComponent->SetWorldLocation(
			FVector(
				UKismetMathLibrary::Clamp(posToClamp.X, boxOrigin.X - boxExtents.X, boxOrigin.X + boxExtents.X),
				UKismetMathLibrary::Clamp(posToClamp.Y, boxOrigin.Y - boxExtents.Y, boxOrigin.Y + boxExtents.Y),
				posToClamp.Z
			)
		);
	}
}

void URTSCamera::updateMinimapFrustum()
{
	/// 战略視野投影核心逻辑：将缩放意图(DesiredZoomLength)直接转化为地平面的投影区域，确保 UI 响应不受物理插值影响
	if (!this->springArmComponent || !this->cameraComponent || !this->rootComponent) return;
	
	const FVector rPos = this->rootComponent->GetComponentLocation();
	const FRotator rRot = this->rootComponent->GetComponentRotation();
	const FRotator aRot = this->springArmComponent->GetRelativeRotation();
	
	/// 合成相机的总逻辑朝向
	const FRotator logicalRotation = rRot + aRot;

	/// 关键点：使用 DesiredZoomLength 预判物理终点位置
	const float intentLength = this->desiredZoomLength; 
	const FVector logicalOrigin = rPos + logicalRotation.Vector() * (-intentLength);

	const float fovValue = this->cameraComponent->FieldOfView;
	float arValue = this->cameraComponent->AspectRatio;
    if(arValue <= 0.0f) arValue = 1.777f; 

	const float hFOV = FMath::DegreesToRadians(fovValue) / 2.0f;
	const float vFOV = FMath::Atan(FMath::Tan(hFOV) / arValue);

	const float tanH = FMath::Tan(hFOV);
	const float tanV = FMath::Tan(vFOV);

	const FVector forwardVector = logicalRotation.Vector();
	const FVector rightVector = FRotationMatrix(logicalRotation).GetScaledAxis(EAxis::Y);
	const FVector upVector = FRotationMatrix(logicalRotation).GetScaledAxis(EAxis::Z);

	/// 计算四个边界射线
	const FVector trDir = (forwardVector + rightVector * tanH + upVector * tanV).GetSafeNormal();
	const FVector tlDir = (forwardVector - rightVector * tanH + upVector * tanV).GetSafeNormal();
	const FVector brDir = (forwardVector + rightVector * tanH - upVector * tanV).GetSafeNormal();
	const FVector blDir = (forwardVector - rightVector * tanH - upVector * tanV).GetSafeNormal();

	const float gZ = this->rootComponent->GetComponentLocation().Z;
	
	auto calcInt = [&](const FVector& ro, const FVector& rd) -> FVector
	{
		if (rd.Z >= -0.001f) return ro + rd * 100000.0f;
		float t = (gZ - ro.Z) / rd.Z;
		if (t < 0.0f) return ro + rd * 100000.0f;
		return ro + t * rd;
	};

	/// 填充静态数据数组，供外部 Widget 直接读取以降低渲染层级的内存开销
	this->minimapFrustumPoints[0] = calcInt(logicalOrigin, tlDir);
	this->minimapFrustumPoints[1] = calcInt(logicalOrigin, trDir);
	this->minimapFrustumPoints[2] = calcInt(logicalOrigin, brDir);
	this->minimapFrustumPoints[3] = calcInt(logicalOrigin, blDir);

	/// 计算完成后发起广播，通知 UI 订阅者执行定向重绘
	this->onMinimapFrustumUpdated.Broadcast();
}
