// Copyright 2024 Jesus Bracho All Rights Reserved.

#define RTS_CAMERA_CPP
#include "RTSCamera.h"
#include "MassBattleMinimapRegion.h"

// 定义 RTSCamera 专用日志分类
DEFINE_LOG_CATEGORY_STATIC(LogRTSCamera, Log, All);

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
// ... (previous lines remain unchanged)
	/// 设置组件基本生存期属性
	PrimaryComponentTick.bCanEverTick = true;
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
	this->minimumZoomBoundaryConstraint = 0.5f;
	this->boundaryTransitionZoneRatio = 0.15f;
	this->bEnableXBoundaryConstraint = true;
	this->bEnableYBoundaryConstraint = true;
	this->currentLateralSocketOffset = 0.0f;
	this->currentVerticalSocketOffset = 0.0f;

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
		this->handleTargetArmLengthInterpolation();
		this->updateFollowPositionIfTargetActive();
		this->applyBoundaryConstraints();
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
	const FRotator relativeRotation = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRotation.Euler().X,
				relativeRotation.Euler().Y,
				relativeRotation.Euler().Z - this->rotationSpeed
			)
		)
	);
	this->updateMinimapFrustum();
}

void URTSCamera::onTurnCameraRightActionTriggered(const FInputActionValue&)
{
	/// 向右执行定量的步进式偏转
	const FRotator relativeRotation = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRotation.Euler().X,
				relativeRotation.Euler().Y,
				relativeRotation.Euler().Z + this->rotationSpeed
			)
		)
	);
	this->updateMinimapFrustum();
}

void URTSCamera::onMoveCameraYAxisActionTriggered(const FInputActionValue& value)
{
	/// 处理纵向平移请求
	this->requestCameraMovement(
		this->rootComponent->GetForwardVector().X,
		this->rootComponent->GetForwardVector().Y,
		value.Get<float>()
	);
}

void URTSCamera::onMoveCameraXAxisActionTriggered(const FInputActionValue& value)
{
	/// 处理横向平移请求
	this->requestCameraMovement(
		this->rootComponent->GetRightVector().X,
		this->rootComponent->GetRightVector().Y,
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
			this->rootComponent->GetRightVector().X,
			this->rootComponent->GetRightVector().Y,
			dragDelta.X
		);

		this->requestCameraMovement(
			this->rootComponent->GetForwardVector().X,
			this->rootComponent->GetForwardVector().Y,
			dragDelta.Y * -1
		);
	}

	/// 清理拖拽标记
	else if (this->isDragging && !value.Get<bool>())
	{
		this->isDragging = false;
	}
}

void URTSCamera::requestCameraMovement(const float xAxisValue, const float yAxisValue, const float movementScale)
{
	/// 将运动请求压入队列，供 Tick 阶段统一消化
	FMoveCameraCommand movementCommand;
	movementCommand.xAxisValue = xAxisValue;
	movementCommand.yAxisValue = yAxisValue;
	movementCommand.movementScale = movementScale;
	this->pendingMovementCommands.Push(movementCommand);
}

void URTSCamera::applyAccumulatedMovementCommands()
{
	/// 执行帧内所有挂起的平移指令并清空
	for (const auto& [xAxisValue, yAxisValue, movementScale] : this->pendingMovementCommands)
	{
		FVector2D directionVector(xAxisValue, yAxisValue);
		directionVector.Normalize();
		directionVector *= this->currentMovementSpeed * movementScale * this->deltaSeconds;
		
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
	
	if (this->cameraComponent != nullptr)
	{
		// 关键：回归绑定模式以消除缩放抖动，且将相对旋转清零以解决 -90 度视角叠加 Bug。
		this->cameraComponent->bUsePawnControlRotation = false;
		this->cameraComponent->SetUsingAbsoluteRotation(false);
		
		// 视角初始化诊断：验证当前俯仰角设定，此时相机将纯粹继承弹簧臂的俯仰。
		UE_LOG(LogRTSCamera, Warning, TEXT("视角初始化诊断 (V9 零叠加版): 起始俯仰角=%.2f, 起始偏航角=%.2f"), this->startingPitchAngle, this->startingYawAngle);

		// 修复 -90 度叠加：相机作为子组件不需要再次设置 -45 度相对俯角。
		this->cameraComponent->SetRelativeRotation(FRotator::ZeroRotator);
		this->cameraComponent->FieldOfView = 45.0f;
	}
}

void URTSCamera::locateMapBoundaryVolumeByTag()
{
	/// 直接在世界中通过类类型检索 AMinimapRegion
	TArray<AActor*> foundActors;
	UGameplayStatics::GetAllActorsOfClass(
		this->GetWorld(),
		AMinimapRegion::StaticClass(),
		foundActors
	);

	if (foundActors.Num() > 0)
	{
		this->movementBoundaryVolume = foundActors[0];
		if (AMinimapRegion* minimapRegion = Cast<AMinimapRegion>(this->movementBoundaryVolume)) 
		{
			const FVector logicalExtent = minimapRegion->BoundsComponent->GetScaledBoxExtent();
			const float mapOverflowDistance = minimapRegion->MapOverflowUU;
			
			// --- 視野溢出定量诊断 (V14 - 全程线性强稳版) ---
			// 我们精确量化高空与低空视角的物理物性，以验证线性斜率 k。
			if (this->cameraComponent != nullptr)
			{
				const float pitchAngleInRadians = FMath::DegreesToRadians(FMath::Abs(this->startingPitchAngle));
				const float horizontalFieldOfViewHalf = FMath::DegreesToRadians(this->cameraComponent->FieldOfView) / 2.0f;
				const FVector2D viewportSize = UWidgetLayoutLibrary::GetViewportSize(this->GetWorld());
				const float viewportAspectRatioValue = (viewportSize.Y > 0.0f) ? (viewportSize.X / viewportSize.Y) : this->cameraComponent->AspectRatio;
				const float verticalFieldOfViewHalf = FMath::Atan(FMath::Tan(horizontalFieldOfViewHalf) / viewportAspectRatioValue);
				
				// 辅助计算闭包函数 (用于获取任意高度下的视野属性)
				auto calculateReachForLength = [&](float length) {
					const float z = length * FMath::Sin(pitchAngleInRadians);
					const float slant = z / FMath::Sin(pitchAngleInRadians + verticalFieldOfViewHalf);
					const float lateralReach = slant * FMath::Tan(horizontalFieldOfViewHalf);
					const float forwardReach = slant * FMath::Cos(pitchAngleInRadians + verticalFieldOfViewHalf);
					return FVector(z, lateralReach, forwardReach);
				};

				const FVector maxPhysics = calculateReachForLength(this->maximumZoomLength); 
				const FVector minPhysics = calculateReachForLength(this->minimumZoomLength);

				const float maxPhysicsX = maxPhysics.Z; // 南北延伸 (Forward Reach)
				const float maxPhysicsY = maxPhysics.Y; // 东西跨度 (Lateral Reach)
				
				// 计算后向延伸 (Backward Reach)
				const float slantBottom = maxPhysics.X / FMath::Sin(pitchAngleInRadians - verticalFieldOfViewHalf);
				const float backwardReach = slantBottom * FMath::Cos(pitchAngleInRadians - verticalFieldOfViewHalf);

				this->lateralReachFactor = maxPhysicsY / this->maximumZoomLength;
				this->forwardReachFactor = maxPhysicsX / this->maximumZoomLength;
				this->backwardReachFactor = backwardReach / this->maximumZoomLength;

				const float lateralAngleDeg = FMath::RadiansToDegrees(FMath::Atan(this->lateralReachFactor));
				const float forwardAngleDeg = FMath::RadiansToDegrees(FMath::Atan(this->forwardReachFactor));
				const float backwardAngleDeg = FMath::RadiansToDegrees(FMath::Atan(this->backwardReachFactor));

				UE_LOG(LogRTSCamera, Warning, TEXT("=== 边界溢出诊断报告 (V14 - 全程线性版) ==="));
				UE_LOG(LogRTSCamera, Warning, TEXT("俯仰基准: %.1f | 溢出阈值 (MapOverflowUU): %.1f"), this->startingPitchAngle, mapOverflowDistance);
				UE_LOG(LogRTSCamera, Warning, TEXT("[预计算系数] Lateral: %.4f (%.2f°) | Forward: %.4f (%.2f°) | Backward: %.4f (%.2f°)"), 
					this->lateralReachFactor, lateralAngleDeg, this->forwardReachFactor, forwardAngleDeg, this->backwardReachFactor, backwardAngleDeg);
				const float minPhysicsX = minPhysics.Z;
				const float minPhysicsY = minPhysics.Y;

				UE_LOG(LogRTSCamera, Warning, TEXT("[高空 Zmax=%.1f] 东西补偿 (Reach-O): %.1f | 南北补偿 (Reach-O): %.1f"), maxPhysics.X, maxPhysicsY - mapOverflowDistance, maxPhysicsX - mapOverflowDistance);
				UE_LOG(LogRTSCamera, Warning, TEXT("[低空 Zmin=%.1f] 东西补偿 (Reach-O): %.1f | 南北补偿 (Reach-O): %.1f"), minPhysics.X, minPhysicsY - mapOverflowDistance, minPhysicsX - mapOverflowDistance);
				UE_LOG(LogRTSCamera, Warning, TEXT("--- 线性偏置计算 (y = kz + b) ---"));
				UE_LOG(LogRTSCamera, Warning, TEXT("东西向 (Y) 补偿范围: %.1f -> %.1f"), minPhysics.Y - mapOverflowDistance, maxPhysics.Y - mapOverflowDistance);
				UE_LOG(LogRTSCamera, Warning, TEXT("南北向 (X) 补偿范围: %.1f -> %.1f"), minPhysics.Z - mapOverflowDistance, maxPhysics.Z - mapOverflowDistance);
				UE_LOG(LogRTSCamera, Warning, TEXT("========================================="));
			}

			// 还原基础初始化信息日志
			UE_LOG(LogRTSCamera, Log, TEXT("RTSCamera 初始化: 挂载 [%s], 逻辑边界: %.1f x %.1f, 溢出保护: %.1f"), 
				*minimapRegion->GetName(), logicalExtent.X * 2.0f, logicalExtent.Y * 2.0f, minimapRegion->MapOverflowUU);
		}
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
	if (const auto enhancedInputComponent = Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent))
	{
		enhancedInputComponent->BindAction(this->zoomCameraAction, ETriggerEvent::Triggered, this, &URTSCamera::onZoomCameraActionTriggered);
		enhancedInputComponent->BindAction(this->rotateCameraAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onRotateCameraActionTriggered);
		enhancedInputComponent->BindAction(this->turnCameraLeftAction, ETriggerEvent::Triggered, this, &URTSCamera::onTurnCameraLeftActionTriggered);
		enhancedInputComponent->BindAction(this->turnCameraRightAction, ETriggerEvent::Triggered, this, &URTSCamera::onTurnCameraRightActionTriggered);
		enhancedInputComponent->BindAction(this->moveCameraXAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraXAxisActionTriggered);
		enhancedInputComponent->BindAction(this->moveCameraYAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraYAxisActionTriggered);
		enhancedInputComponent->BindAction(this->dragCameraAction, ETriggerEvent::Triggered, this, &URTSCamera::onDragCameraActionTriggered);
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
	this->applyBoundaryConstraints();
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
	const auto mousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto viewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedValue = 1 - UKismetMathLibrary::NormalizeToRange(mousePosition.X, 0.0f, viewportSize.X * this->distanceFromEdgeThreshold);

	const float alpha = UKismetMathLibrary::FClamp(normalizedValue, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(-1 * this->rootComponent->GetRightVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollRight()
{
	/// 基于鼠标右偏移计算平移推力
	const auto mousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto viewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedValue = UKismetMathLibrary::NormalizeToRange(mousePosition.X, viewportSize.X * (1 - this->distanceFromEdgeThreshold), viewportSize.X);

	const float alpha = UKismetMathLibrary::FClamp(normalizedValue, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(this->rootComponent->GetRightVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollUp()
{
	/// 基于鼠标上偏移计算平移推力
	const auto mousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto viewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedValue = UKismetMathLibrary::NormalizeToRange(mousePosition.Y, 0.0f, viewportSize.Y * this->distanceFromEdgeThreshold);

	const float alpha = 1 - UKismetMathLibrary::FClamp(normalizedValue, 0.0, 1.0);
	this->rootComponent->AddRelativeLocation(this->rootComponent->GetForwardVector() * alpha * this->currentMovementSpeed * this->deltaSeconds);
}

void URTSCamera::performEdgeScrollDown()
{
	/// 基于鼠标下偏移计算平移推力
	const auto mousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto viewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto normalizedValue = UKismetMathLibrary::NormalizeToRange(mousePosition.Y, viewportSize.Y * (1 - this->distanceFromEdgeThreshold), viewportSize.Y);

	const float alpha = UKismetMathLibrary::FClamp(normalizedValue, 0.0, 1.0);
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
		const TArray<AActor*> excludedActors;

		FHitResult floorHit;
		const bool bValidFloor = UKismetSystemLibrary::LineTraceSingle(
			this->GetWorld(),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z + this->findGroundTraceLength),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z - this->findGroundTraceLength),
			UEngineTypes::ConvertToTraceType(this->collisionChannel),
			true,
			excludedActors,
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


void URTSCamera::updateMinimapFrustum()
{
	/// 战略視野投影核心逻辑：直接基于相机的世界位姿计算四个角点。
	if (this->cameraComponent == nullptr || this->rootComponent == nullptr) 
	{
		return;
	}
	
	// 核心：强制触发 SpringArm 及其子组件的世界变换计算
	// 否则在同一帧内，GetComponentLocation 拿到的可能是修改 SocketOffset 之前的旧位置
	// 但是我不认可这种逻辑，因为对小地图来说这不重要，我觉得是无稽之谈
	// if (this->springArmComponent)
	// {
	// 	this->springArmComponent->UpdateComponentToWorld();
	// }
	// this->cameraComponent->UpdateComponentToWorld();

	const FVector cameraLocation = this->cameraComponent->GetComponentLocation();
	const FRotator cameraRotation = this->cameraComponent->GetComponentRotation();
	
	const float fieldOfViewValue = this->cameraComponent->FieldOfView;
	
	// 拒绝假设：动态获取视口尺寸计算长宽比
	const FVector2D viewportSize = UWidgetLayoutLibrary::GetViewportSize(this->GetWorld());
	float aspectRatioValue = (viewportSize.Y > 0.0f) ? (viewportSize.X / viewportSize.Y) : this->cameraComponent->AspectRatio;
	if (this->cameraComponent->bConstrainAspectRatio)
	{
		aspectRatioValue = this->cameraComponent->AspectRatio;
	}

	const float horizontalFieldOfView = FMath::DegreesToRadians(fieldOfViewValue) / 2.0f;
	const float verticalFieldOfView = FMath::Atan(FMath::Tan(horizontalFieldOfView) / aspectRatioValue);

	const float tangentHorizontal = FMath::Tan(horizontalFieldOfView);
	const float tangentVertical = FMath::Tan(verticalFieldOfView);

	const FVector forwardVector = cameraRotation.Vector();
	const FVector rightVector = FRotationMatrix(cameraRotation).GetScaledAxis(EAxis::Y);
	const FVector upVector = FRotationMatrix(cameraRotation).GetScaledAxis(EAxis::Z);

	/// 计算四个边界射线
	const FVector topRightDirection = (forwardVector + rightVector * tangentHorizontal + upVector * tangentVertical).GetSafeNormal();
	const FVector topLeftDirection = (forwardVector - rightVector * tangentHorizontal + upVector * tangentVertical).GetSafeNormal();
	const FVector bottomRightDirection = (forwardVector + rightVector * tangentHorizontal - upVector * tangentVertical).GetSafeNormal();
	const FVector bottomLeftDirection = (forwardVector - rightVector * tangentHorizontal - upVector * tangentVertical).GetSafeNormal();

	const float groundAltitude = this->rootComponent->GetComponentLocation().Z;
	
	auto calculateIntersection = [&](const FVector& rayOrigin, const FVector& rayDirection) -> FVector
	{
		if (rayDirection.Z >= -0.001f) 
		{
			return rayOrigin + rayDirection * 100000.0f;
		}

		const float timeToIntersection = (groundAltitude - rayOrigin.Z) / rayDirection.Z;
		if (timeToIntersection < 0.0f) 
		{
			return rayOrigin + rayDirection * 100000.0f;
		}

		return rayOrigin + rayDirection * timeToIntersection;
	};

	/// 填充战略投影点数组
	this->minimapFrustumPoints[0] = calculateIntersection(cameraLocation, topLeftDirection);
	this->minimapFrustumPoints[1] = calculateIntersection(cameraLocation, topRightDirection);
	this->minimapFrustumPoints[2] = calculateIntersection(cameraLocation, bottomRightDirection);
	this->minimapFrustumPoints[3] = calculateIntersection(cameraLocation, bottomLeftDirection);

	/// 计算完成后发起视野更新广播
	this->onMinimapFrustumUpdated.Broadcast();
}

void URTSCamera::applyBoundaryConstraints()
{
	if (this->movementBoundaryVolume == nullptr || this->springArmComponent == nullptr)
	{
		return;
	}

	// 1. 获取边界数据
	FVector boxOrigin = FVector::ZeroVector;
	FVector boxExtents = FVector::ZeroVector;
	if (const AMinimapRegion* minimapRegion = Cast<AMinimapRegion>(this->movementBoundaryVolume))
	{
		if (minimapRegion->BoundsComponent != nullptr)
		{
			boxExtents = minimapRegion->BoundsComponent->GetScaledBoxExtent();
			boxOrigin = minimapRegion->BoundsComponent->GetComponentLocation();
		}
	}

	if (boxExtents.IsZero()) return;

	// 2. 地形高度同步 (取代 Tick 中的独立调用)
	this->rectifyRootHeightFromTerrain();

	// 3. 计算并应用偏移
	const FVector currentPos = this->rootComponent->GetComponentLocation();
	this->currentLateralSocketOffset = this->calculateYOffset(currentPos.Y);
	this->currentVerticalSocketOffset = this->calculateXOffset(currentPos.X);

	this->springArmComponent->SocketOffset = FVector(this->currentVerticalSocketOffset, this->currentLateralSocketOffset, 0.0f);

	// 4. Root 物理坐标锁定 (核心：边界限制永远生效，Flag 仅控制是否产生 Offset)
	FVector clampedLocation = currentPos;
	clampedLocation.X = FMath::Clamp(clampedLocation.X, boxOrigin.X - boxExtents.X, boxOrigin.X + boxExtents.X);
	clampedLocation.Y = FMath::Clamp(clampedLocation.Y, boxOrigin.Y - boxExtents.Y, boxOrigin.Y + boxExtents.Y);
	
	this->rootComponent->SetWorldLocation(clampedLocation);
}

float URTSCamera::calculateYOffset(float worldY) const
{
	if (!this->bEnableYBoundaryConstraint || this->movementBoundaryVolume == nullptr) return 0.0f;

	const AMinimapRegion* minimapRegion = Cast<AMinimapRegion>(this->movementBoundaryVolume);
	if (!minimapRegion || !minimapRegion->BoundsComponent) return 0.0f;

	const FVector boxOrigin = minimapRegion->BoundsComponent->GetComponentLocation();
	const FVector boxExtents = minimapRegion->BoundsComponent->GetScaledBoxExtent();
	const float mapOverflow = minimapRegion->MapOverflowUU;

	const float differenceY = worldY - boxOrigin.Y;
	const float normalizedDistanceY = FMath::Abs(differenceY) / FMath::Max(boxExtents.Y, 1.0f);
	const float safeZoneRatio = 1.0f - this->boundaryTransitionZoneRatio;

	if (normalizedDistanceY > safeZoneRatio)
	{
		const float triggerAlpha = (normalizedDistanceY - safeZoneRatio) / FMath::Max(this->boundaryTransitionZoneRatio, 0.01f);
		const float reach = this->springArmComponent->TargetArmLength * this->lateralReachFactor;
		const float offset = triggerAlpha * (reach * this->minimumZoomBoundaryConstraint) * ((differenceY > 0.0f) ? -1.0f : 1.0f);
		
		UE_LOG(LogRTSCamera, VeryVerbose, TEXT("横向 (Y) 比例补偿: alpha=%.2f, reach=%.1f, offset=%.1f"), triggerAlpha, reach, offset);
		return offset;
	}

	return 0.0f;
}

float URTSCamera::calculateXOffset(float worldX) const
{
	if (!this->bEnableXBoundaryConstraint || this->movementBoundaryVolume == nullptr) return 0.0f;

	const AMinimapRegion* minimapRegion = Cast<AMinimapRegion>(this->movementBoundaryVolume);
	if (!minimapRegion || !minimapRegion->BoundsComponent) return 0.0f;

	const FVector boxOrigin = minimapRegion->BoundsComponent->GetComponentLocation();
	const FVector boxExtents = minimapRegion->BoundsComponent->GetScaledBoxExtent();
	const float mapOverflow = minimapRegion->MapOverflowUU;

	const float differenceX = worldX - boxOrigin.X;
	const float normalizedDistanceX = FMath::Abs(differenceX) / FMath::Max(boxExtents.X, 1.0f);
	const float safeZoneRatio = 1.0f - this->boundaryTransitionZoneRatio;

	if (normalizedDistanceX > safeZoneRatio)
	{
		const float triggerAlpha = (normalizedDistanceX - safeZoneRatio) / FMath::Max(this->boundaryTransitionZoneRatio, 0.01f);
		
		// 南北向由于 Pitch 倾角是不对称的
		// 北端 (diff > 0) 使用前进伸展 forwardReach；南端 (diff < 0) 使用后退伸展 backwardReach
		const float currentFactor = (differenceX > 0.0f) ? this->forwardReachFactor : this->backwardReachFactor;
		const float reach = this->springArmComponent->TargetArmLength * currentFactor;

		// 核心逻辑：
		// 1. 在北端 (diff > 0)，我们需要向南 (Negative X) 偏移，把视口顶部的地图拉回来。
		// 2. 在南端 (diff < 0)，我们需要向北 (Positive X) 偏移，把视口底部的地图边界拉进来。
		const float direction = (differenceX > 0.0f) ? -1.0f : 1.0f;
		const float offset = direction * triggerAlpha * (reach * this->minimumZoomBoundaryConstraint);

		UE_LOG(LogRTSCamera, VeryVerbose, TEXT("南北 (X) 修正方案: diff=%.1f, factor=%.4f, offset=%.1f"), differenceX, currentFactor, offset);
		return offset;
	}

	return 0.0f;
}
