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
	/// 开启组件更新并设置默认相机策略参数
	PrimaryComponentTick.bCanEverTick = true;
	this->CameraBlockingVolumeTag = FName("OpenRTSCamera#CameraBounds");
	this->CollisionChannel = ECC_WorldStatic;
	this->DragExtent = 0.6f;
	this->DistanceFromEdgeThreshold = 0.1f;
	this->EnableCameraLag = true;
	this->EnableCameraRotationLag = true;
	this->EnableDynamicCameraHeight = true;
	this->EnableEdgeScrolling = true;
	this->FindGroundTraceLength = 100000;
	this->MaximumZoomLength = 5000;
	this->MinimumZoomLength = 500;
	this->MaxMoveSpeed = 1024.0f;
	this->MinMoveSpeed = 128.0f;
	this->CurrentMovementSpeed = this->MinMoveSpeed; 
	this->RotateSpeed = 45;
	this->StartingYAngle = -45.0f;
	this->StartingZAngle = 0;
	this->ZoomCatchupSpeed = 4;
	this->ZoomSpeed = -200;

	/// 加载并绑定 Enhanced Input 相关资源
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveCameraXAxisFinder(TEXT("/OpenRTSCamera/Inputs/MoveCameraXAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveCameraYAxisFinder(TEXT("/OpenRTSCamera/Inputs/MoveCameraYAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RotateCameraAxisFinder(TEXT("/OpenRTSCamera/Inputs/RotateCameraAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> TurnCameraLeftFinder(TEXT("/OpenRTSCamera/Inputs/TurnCameraLeft"));
	static ConstructorHelpers::FObjectFinder<UInputAction> TurnCameraRightFinder(TEXT("/OpenRTSCamera/Inputs/TurnCameraRight"));
	static ConstructorHelpers::FObjectFinder<UInputAction> ZoomCameraFinder(TEXT("/OpenRTSCamera/Inputs/ZoomCamera"));
	static ConstructorHelpers::FObjectFinder<UInputAction> DragCameraFinder(TEXT("/OpenRTSCamera/Inputs/DragCamera"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> InputMappingContextFinder(TEXT("/OpenRTSCamera/Inputs/OpenRTSCameraInputs"));

	this->MoveCameraXAxis = MoveCameraXAxisFinder.Object;
	this->MoveCameraYAxis = MoveCameraYAxisFinder.Object;
	this->RotateCameraAxis = RotateCameraAxisFinder.Object;
	this->TurnCameraLeft = TurnCameraLeftFinder.Object;
	this->TurnCameraRight = TurnCameraRightFinder.Object;
	this->DragCamera = DragCameraFinder.Object;
	this->ZoomCamera = ZoomCameraFinder.Object;
	this->InputMappingContext = InputMappingContextFinder.Object;
}

void URTSCamera::BeginPlay()
{
	Super::BeginPlay();

	const auto NetMode = this->GetNetMode();
	if (NetMode != NM_DedicatedServer)
	{
		/// 初始化依赖组件、配置弹簧臂并寻找地图边界
		this->CollectComponentDependencyReferences();
		this->ConfigureSpringArm();
		this->TryToFindBoundaryVolumeReference();
		this->ConditionallyEnableEdgeScrolling();
		this->CheckForEnhancedInputComponent();
		this->BindInputMappingContext();
		this->BindInputActions();
	}
}

void URTSCamera::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const auto NetMode = this->GetNetMode();
	
	/// 仅在非专用服务器且当前相机为活跃视图时执行状态更新 (移动、缩放、边界检查等)
	if (NetMode != NM_DedicatedServer && this->RTSPlayerController->GetViewTarget() == this->CameraOwner)
	{
		this->DeltaSeconds = DeltaTime;
		this->ApplyMoveCameraCommands();
		this->ConditionallyPerformEdgeScrolling();
		this->ConditionallyKeepCameraAtDesiredZoomAboveGround();
		this->SmoothTargetArmLengthToDesiredZoom();
		this->FollowTargetIfSet();
		this->ConditionallyApplyCameraBounds();
	}
}
void URTSCamera::FollowTarget(AActor* Target)
{
	this->CameraFollowTarget = Target;
}

void URTSCamera::UnFollowTarget()
{
	this->CameraFollowTarget = nullptr;
}

void URTSCamera::OnZoomCamera(const FInputActionValue& Value)
{
	/// 更新意图缩放距离
	this->DesiredZoomLength = FMath::Clamp(
		this->DesiredZoomLength + Value.Get<float>() * this->ZoomSpeed,
		this->MinimumZoomLength,
		this->MaximumZoomLength
	);

	/// 计算缩放比例并动态调整移动感官速度
	float Alpha = (this->DesiredZoomLength - this->MinimumZoomLength) / (this->MaximumZoomLength - this->MinimumZoomLength);
	this->CurrentMovementSpeed = FMath::Lerp(this->MinMoveSpeed, this->MaxMoveSpeed, Alpha);

	/// 基于意图高度即时刷新小地图视野框
	this->UpdateMinimapFrustum();
}

void URTSCamera::OnRotateCamera(const FInputActionValue& Value)
{
	/// 获取当前根组件旋转并应用水平旋转增量
	const auto WorldRotation = this->RootComponent->GetComponentRotation();
	this->RootComponent->SetWorldRotation(
		FRotator::MakeFromEuler(
			FVector(
				WorldRotation.Euler().X,
				WorldRotation.Euler().Y,
				WorldRotation.Euler().Z + Value.Get<float>()
			)
		)
	);
	this->UpdateMinimapFrustum();
}

void URTSCamera::OnTurnCameraLeft(const FInputActionValue&)
{
	/// 向左平滑旋转相机
	const auto WorldRotation = this->RootComponent->GetRelativeRotation();
	this->RootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				WorldRotation.Euler().X,
				WorldRotation.Euler().Y,
				WorldRotation.Euler().Z - this->RotateSpeed
			)
		)
	);
	this->UpdateMinimapFrustum();
}

void URTSCamera::OnTurnCameraRight(const FInputActionValue&)
{
	/// 向右平滑旋转相机
	const auto WorldRotation = this->RootComponent->GetRelativeRotation();
	this->RootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				WorldRotation.Euler().X,
				WorldRotation.Euler().Y,
				WorldRotation.Euler().Z + this->RotateSpeed
			)
		)
	);
	this->UpdateMinimapFrustum();
}

void URTSCamera::OnMoveCameraYAxis(const FInputActionValue& Value)
{
	/// 根据相机当前朝向请求垂直（Y轴）移动指令
	this->RequestMoveCamera(
		this->SpringArmComponent->GetForwardVector().X,
		this->SpringArmComponent->GetForwardVector().Y,
		Value.Get<float>()
	);
}

void URTSCamera::OnMoveCameraXAxis(const FInputActionValue& Value)
{
	/// 根据相机当前朝向请求水平（X轴）移动指令
	this->RequestMoveCamera(
		this->SpringArmComponent->GetRightVector().X,
		this->SpringArmComponent->GetRightVector().Y,
		Value.Get<float>()
	);
}

void URTSCamera::OnDragCamera(const FInputActionValue& Value)
{
	/// 开始拖拽：记录初始鼠标位置
	if (!this->IsDragging && Value.Get<bool>())
	{
		this->IsDragging = true;
		this->DragStartLocation = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	}

	/// 拖拽中：计算位置增量并映射到移动指令
	else if (this->IsDragging && Value.Get<bool>())
	{
		const auto MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
		auto DragExtents = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
		DragExtents *= DragExtent;

		auto Delta = MousePosition - this->DragStartLocation;
		Delta.X = FMath::Clamp(Delta.X, -DragExtents.X, DragExtents.X) / DragExtents.X;
		Delta.Y = FMath::Clamp(Delta.Y, -DragExtents.Y, DragExtents.Y) / DragExtents.Y;

		this->RequestMoveCamera(
			this->SpringArmComponent->GetRightVector().X,
			this->SpringArmComponent->GetRightVector().Y,
			Delta.X
		);

		this->RequestMoveCamera(
			this->SpringArmComponent->GetForwardVector().X,
			this->SpringArmComponent->GetForwardVector().Y,
			Delta.Y * -1
		);
	}

	/// 停止拖拽
	else if (this->IsDragging && !Value.Get<bool>())
	{
		this->IsDragging = false;
	}
}

void URTSCamera::RequestMoveCamera(const float X, const float Y, const float Scale)
{
	FMoveCameraCommand MoveCameraCommand;
	MoveCameraCommand.X = X;
	MoveCameraCommand.Y = Y;
	MoveCameraCommand.Scale = Scale;
	MoveCameraCommands.Push(MoveCameraCommand);
}

void URTSCamera::ApplyMoveCameraCommands()
{
	/// 遍历执行积压的移动指令，应用当前速度并完成平移
	for (const auto& [X, Y, Scale] : this->MoveCameraCommands)
	{
		auto Movement = FVector2D(X, Y);
		Movement.Normalize();
		Movement *= this->CurrentMovementSpeed * Scale * this->DeltaSeconds;
		
		this->JumpTo(
			this->RootComponent->GetComponentLocation() + FVector(Movement.X, Movement.Y, 0.0f)
		);
	}

	this->MoveCameraCommands.Empty();
}

void URTSCamera::CollectComponentDependencyReferences()
{
	/// 缓存所有者及其核心组件指针以供快速访问
	this->CameraOwner = this->GetOwner();
	this->RootComponent = this->CameraOwner->GetRootComponent();
	this->CameraComponent = Cast<UCameraComponent>(this->CameraOwner->GetComponentByClass(UCameraComponent::StaticClass()));
	this->SpringArmComponent = Cast<USpringArmComponent>(this->CameraOwner->GetComponentByClass(USpringArmComponent::StaticClass()));
	this->RTSPlayerController = UGameplayStatics::GetPlayerController(this->GetWorld(), 0);
}

void URTSCamera::ConfigureSpringArm()
{
	/// 初始化弹簧臂臂长及旋转参数
	this->DesiredZoomLength = this->MinimumZoomLength;
	this->SpringArmComponent->TargetArmLength = this->DesiredZoomLength;
	this->SpringArmComponent->bDoCollisionTest = false;
	this->SpringArmComponent->bEnableCameraLag = this->EnableCameraLag;
	this->SpringArmComponent->bEnableCameraRotationLag = this->EnableCameraRotationLag;
	this->SpringArmComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				0.0,
				this->StartingYAngle,
				this->StartingZAngle
			)
		)
	);
}

void URTSCamera::TryToFindBoundaryVolumeReference()
{
	/// 通过标签检索地图中的相机边界体积
	TArray<AActor*> BlockingVolumes;
	UGameplayStatics::GetAllActorsOfClassWithTag(
		this->GetWorld(),
		AActor::StaticClass(),
		this->CameraBlockingVolumeTag,
		BlockingVolumes
	);

	if (BlockingVolumes.Num() > 0)
	{
		this->BoundaryVolume = BlockingVolumes[0];
		
		/// 初始化预分配视野点数组并触发初次刷新
		MinimapFrustumPoints.SetNum(4);
		this->UpdateMinimapFrustum();
	}
}

void URTSCamera::ConditionallyEnableEdgeScrolling()
{
	/// 如已启用，则将鼠标锁定并在视口中显示
	if (this->EnableEdgeScrolling)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		InputMode.SetHideCursorDuringCapture(false);
		this->RTSPlayerController->SetInputMode(InputMode);
	}
}

void URTSCamera::CheckForEnhancedInputComponent()
{
	/// 检查是否配置了增强输入组件，若缺失则打印视觉错误提示
	if (Cast<UEnhancedInputComponent>(this->RTSPlayerController->InputComponent) == nullptr)
	{
		UKismetSystemLibrary::PrintString(
			this->GetWorld(),
			TEXT("Error: Enhanced input component not found. Check Project Settings > Input."), true, true,
			FLinearColor::Red,
			100
		);
	}
}

void URTSCamera::BindInputMappingContext()
{
	/// 绑定增强输入上下文，并显示鼠标光标
	if (RTSPlayerController && RTSPlayerController->GetLocalPlayer())
	{
		if (const auto Input = RTSPlayerController->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			RTSPlayerController->bShowMouseCursor = true;

			if (!Input->HasMappingContext(this->InputMappingContext))
			{
				Input->AddMappingContext(this->InputMappingContext, 0);
			}
		}
	}
}

void URTSCamera::BindInputActions()
{
	/// 将输入动作事件绑定到本地处理函数
	if (const auto EnhancedInputComponent = Cast<UEnhancedInputComponent>(this->RTSPlayerController->InputComponent))
	{
		EnhancedInputComponent->BindAction(
			this->ZoomCamera,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnZoomCamera
		);

		EnhancedInputComponent->BindAction(
			this->RotateCameraAxis,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnRotateCamera
		);

		EnhancedInputComponent->BindAction(
			this->TurnCameraLeft,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnTurnCameraLeft
		);

		EnhancedInputComponent->BindAction(
			this->TurnCameraRight,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnTurnCameraRight
		);

		EnhancedInputComponent->BindAction(
			this->MoveCameraXAxis,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnMoveCameraXAxis
		);

		EnhancedInputComponent->BindAction(
			this->MoveCameraYAxis,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnMoveCameraYAxis
		);

		EnhancedInputComponent->BindAction(
			this->DragCamera,
			ETriggerEvent::Triggered,
			this,
			&URTSCamera::OnDragCamera
		);
	}
}

void URTSCamera::SetActiveCamera()
{
	/// 将当前控制器的观察目标设置为本组件的所有者
	this->RTSPlayerController->SetViewTarget(this->GetOwner());
}

void URTSCamera::JumpTo(const FVector Position)
{
	/// 保持当前高度不变，更新水平位置并刷新视野
	float CurrentZ = this->RootComponent->GetComponentLocation().Z;
	this->RootComponent->SetWorldLocation(FVector(Position.X, Position.Y, CurrentZ));
	this->UpdateMinimapFrustum();
}

void URTSCamera::ConditionallyPerformEdgeScrolling()
{
	/// 如已启用边缘滚动且当前未处于鼠标拖拽状态，则执行视口边缘推进逻辑
	if (this->EnableEdgeScrolling && !this->IsDragging)
	{
		const FVector OldLocation = this->RootComponent->GetComponentLocation();
		
		this->EdgeScrollLeft();
		this->EdgeScrollRight();
		this->EdgeScrollUp();
		this->EdgeScrollDown();

		/// 位置发生变动时立即同步视野框
		if (!this->RootComponent->GetComponentLocation().Equals(OldLocation, 0.1f))
		{
			this->UpdateMinimapFrustum();
		}
	}
}

void URTSCamera::EdgeScrollLeft()
{
	/// 计算鼠标在左侧边缘的深度，并以此确定摄像机左移量
	const auto MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto ViewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto NormalizedPosition = 1 - UKismetMathLibrary::NormalizeToRange(
		MousePosition.X,
		0.0f,
		ViewportSize.X * this->DistanceFromEdgeThreshold
	);

	const float MovementAlpha = UKismetMathLibrary::FClamp(NormalizedPosition, 0.0, 1.0);

	this->RootComponent->AddRelativeLocation(
		-1 * this->RootComponent->GetRightVector() * MovementAlpha * this->CurrentMovementSpeed * this->DeltaSeconds
	);
}

void URTSCamera::EdgeScrollRight()
{
	/// 计算鼠标在右侧边缘的深度，并以此确定摄像机右移量
	const auto MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto ViewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto NormalizedPosition = UKismetMathLibrary::NormalizeToRange(
		MousePosition.X,
		ViewportSize.X * (1 - this->DistanceFromEdgeThreshold),
		ViewportSize.X
	);

	const float MovementAlpha = UKismetMathLibrary::FClamp(NormalizedPosition, 0.0, 1.0);
	this->RootComponent->AddRelativeLocation(
		this->RootComponent->GetRightVector() * MovementAlpha * this->CurrentMovementSpeed * this->DeltaSeconds
	);
}

void URTSCamera::EdgeScrollUp()
{
	/// 计算鼠标在上侧边缘的深度，并以此确定摄像机前移量
	const auto MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto ViewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto NormalizedPosition = UKismetMathLibrary::NormalizeToRange(
		MousePosition.Y,
		0.0f,
		ViewportSize.Y * this->DistanceFromEdgeThreshold
	);

	const float MovementAlpha = 1 - UKismetMathLibrary::FClamp(NormalizedPosition, 0.0, 1.0);
	this->RootComponent->AddRelativeLocation(
		this->RootComponent->GetForwardVector() * MovementAlpha * this->CurrentMovementSpeed * this->DeltaSeconds
	);
}

void URTSCamera::EdgeScrollDown()
{
	/// 计算鼠标在下侧边缘的深度，并以此确定摄像机后移量
	const auto MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld());
	const auto ViewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld()).GetLocalSize();
	const auto NormalizedPosition = UKismetMathLibrary::NormalizeToRange(
		MousePosition.Y,
		ViewportSize.Y * (1 - this->DistanceFromEdgeThreshold),
		ViewportSize.Y
	);

	const float MovementAlpha = UKismetMathLibrary::FClamp(NormalizedPosition, 0.0, 1.0);
	this->RootComponent->AddRelativeLocation(
		-1 * this->RootComponent->GetForwardVector() * MovementAlpha * this->CurrentMovementSpeed * this->DeltaSeconds
	);
}

void URTSCamera::FollowTargetIfSet()
{
	if (this->CameraFollowTarget != nullptr)
	{
		this->JumpTo(this->CameraFollowTarget->GetActorLocation());
	}
}

void URTSCamera::SmoothTargetArmLengthToDesiredZoom()
{
	/// 使用平滑插值算法将弹簧臂长度拉近至目标意图长度
	float OldArmLength = this->SpringArmComponent->TargetArmLength;
	
	this->SpringArmComponent->TargetArmLength = FMath::FInterpTo(
		this->SpringArmComponent->TargetArmLength,
		this->DesiredZoomLength,
		this->DeltaSeconds,
		this->ZoomCatchupSpeed
	);
}

void URTSCamera::ConditionallyKeepCameraAtDesiredZoomAboveGround()
{
	/// 动态调整相机高度以贴合地形
	if (this->EnableDynamicCameraHeight)
	{
		const FVector RootWorldLocation = this->RootComponent->GetComponentLocation();
		const TArray<AActor*> ActorsToIgnore;

		FHitResult HitResult;
		bool bDidHit = UKismetSystemLibrary::LineTraceSingle(
			this->GetWorld(),
			FVector(RootWorldLocation.X, RootWorldLocation.Y, RootWorldLocation.Z + this->FindGroundTraceLength),
			FVector(RootWorldLocation.X, RootWorldLocation.Y, RootWorldLocation.Z - this->FindGroundTraceLength),
			UEngineTypes::ConvertToTraceType(this->CollisionChannel),
			true,
			ActorsToIgnore,
			EDrawDebugTrace::Type::None,
			HitResult,
			true
		);

		if (bDidHit)
		{
			this->RootComponent->SetWorldLocation(
				FVector(
					HitResult.Location.X,
					HitResult.Location.Y,
					HitResult.Location.Z
				)
			);
		}
	}
}

void URTSCamera::ConditionallyApplyCameraBounds()
{
	/// 如已定义边界，则将当前位置限制在边界体积范围内
	if (this->BoundaryVolume != nullptr)
	{
		const FVector CurrentWorldLocation = this->RootComponent->GetComponentLocation();
		FVector Origin;
		FVector Extents;
		this->BoundaryVolume->GetActorBounds(false, Origin, Extents);

		this->RootComponent->SetWorldLocation(
			FVector(
				UKismetMathLibrary::Clamp(CurrentWorldLocation.X, Origin.X - Extents.X, Origin.X + Extents.X),
				UKismetMathLibrary::Clamp(CurrentWorldLocation.Y, Origin.Y - Extents.Y, Origin.Y + Extents.Y),
				CurrentWorldLocation.Z
			)
		);
	}
}

void URTSCamera::UpdateMinimapFrustum()
{
	if (!SpringArmComponent || !CameraComponent || !RootComponent) return;
	
	/// 1. 提取当前相机的策略参数 (X, Y, Z_Intent, Pitch, Yaw)
	FVector RootLocation = RootComponent->GetComponentLocation();
	FRotator RootRotation = RootComponent->GetComponentRotation();
	FRotator ArmRotation = SpringArmComponent->GetRelativeRotation();
	
	/// 合成逻辑相机旋转 (使用根组件的 Yaw 和弹簧臂的 Pitch)
	FRotator LogicCameraRotation = RootRotation + ArmRotation;

	/// 2. 使用意图高度 (DesiredZoomLength) 计算相机逻辑位置
	float CameraArmLength = this->DesiredZoomLength; 
	FVector LogicCameraLocation = RootLocation + LogicCameraRotation.Vector() * (-CameraArmLength);

	/// 3. 获取相机内参并计算视野张角
	float FieldOfView = CameraComponent->FieldOfView;
	float AspectRatio = CameraComponent->AspectRatio;
    if(AspectRatio <= 0.0f) AspectRatio = 1.777f; 

	float HalfHorizontalFOV = FMath::DegreesToRadians(FieldOfView) / 2.0f;
	float HalfVerticalFOV = FMath::Atan(FMath::Tan(HalfHorizontalFOV) / AspectRatio);

	float TangentHorizontal = FMath::Tan(HalfHorizontalFOV);
	float TangentVertical = FMath::Tan(HalfVerticalFOV);

	/// 4. 计算视野四个方向的射线向量
	FVector ForwardVector = LogicCameraRotation.Vector();
	FVector RightVector = FRotationMatrix(LogicCameraRotation).GetScaledAxis(EAxis::Y);
	FVector UpVector = FRotationMatrix(LogicCameraRotation).GetScaledAxis(EAxis::Z);

	FVector DirectionTopLeft = (ForwardVector - RightVector * TangentHorizontal + UpVector * TangentVertical).GetSafeNormal();
	FVector DirectionTopRight = (ForwardVector + RightVector * TangentHorizontal + UpVector * TangentVertical).GetSafeNormal();
	FVector DirectionBottomLeft = (ForwardVector - RightVector * TangentHorizontal - UpVector * TangentVertical).GetSafeNormal();
	FVector DirectionBottomRight = (ForwardVector + RightVector * TangentHorizontal - UpVector * TangentVertical).GetSafeNormal();

	/// 5. 计算射线与地平面的交点进行位置投射
	float GroundZ = this->RootComponent->GetComponentLocation().Z;
	
	auto IntersectGround = [&](const FVector& RayOrigin, const FVector& RayDirection) -> FVector
	{
		if (RayDirection.Z >= -0.001f) return RayOrigin + RayDirection * 100000.0f;
		float t = (GroundZ - RayOrigin.Z) / RayDirection.Z;
		if (t < 0.0f) return RayOrigin + RayDirection * 100000.0f;
		return RayOrigin + t * RayDirection;
	};

	/// 6. 更新视野点并缓存同步至 UI
	if (MinimapFrustumPoints.Num() != 4)
	{
		MinimapFrustumPoints.SetNum(4);
	}

	MinimapFrustumPoints[0] = IntersectGround(LogicCameraLocation, DirectionTopLeft);
	MinimapFrustumPoints[1] = IntersectGround(LogicCameraLocation, DirectionTopRight);
	MinimapFrustumPoints[2] = IntersectGround(LogicCameraLocation, DirectionBottomRight);
	MinimapFrustumPoints[3] = IntersectGround(LogicCameraLocation, DirectionBottomLeft);
}
