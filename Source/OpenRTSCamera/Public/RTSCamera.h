// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "InputMappingContext.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "RTSCamera.generated.h"

/**
 * We use these commands so that move camera inputs can be tied to the tick rate of the game.
 * https://github.com/HeyZoos/OpenRTSCamera/issues/27
 */
USTRUCT()
struct FMoveCameraCommand
{
	GENERATED_BODY()
	UPROPERTY()
	float X = 0;
	UPROPERTY()
	float Y = 0;
	UPROPERTY()
	float Scale = 0;
};

/**
 * @brief       RTS 相机组件，负责处理相机的移动、旋转、缩放以及视野计算。
 * 
 * 该组件集成了平滑缩放、边缘滚动、目标跟随等功能，并能实时计算相机在地面的视野框点。
 **/
UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class OPENRTSCAMERA_API URTSCamera : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSCamera();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void FollowTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void UnFollowTarget();

	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void SetActiveCamera();
	
	/**
	 * @brief       将相机瞬间移动到指定位置
	 * 
	 * @param       参数名称: Position                      数据类型:        FVector
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void JumpTo(FVector Position);

	UFUNCTION(BlueprintPure, Category = "RTSCamera")
	AActor* GetBoundaryVolume() const { return BoundaryVolume; }

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float MinimumZoomLength;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float MaximumZoomLength;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float ZoomCatchupSpeed;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float ZoomSpeed;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float StartingYAngle;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float StartingZAngle;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float MaxMoveSpeed;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float MinMoveSpeed;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float RotateSpeed;
	
	/**
	 * Controls how fast the drag will move the camera.
	 * Higher values will make the camera move more slowly.
	 * The drag speed is calculated as follows:
	 *	DragSpeed = MousePositionDelta / (ViewportExtents * DragExtent)
	 * If the drag extent is small, the drag speed will hit the "max speed" of `this->MoveSpeed` more quickly.
	 */
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera",
		meta = (ClampMin = "0.0", ClampMax = "1.0")
	)
	float DragExtent;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	bool EnableCameraLag;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	bool EnableCameraRotationLag;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	bool EnableDynamicCameraHeight;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	TEnumAsByte<ECollisionChannel> CollisionChannel;
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Dynamic Camera Height Settings",
		meta=(EditCondition="EnableDynamicCameraHeight")
	)
	float FindGroundTraceLength;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Edge Scroll Settings")
	bool EnableEdgeScrolling;


	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Edge Scroll Settings",
		meta=(EditCondition="EnableEdgeScrolling")
	)
	float DistanceFromEdgeThreshold;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputMappingContext* InputMappingContext;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* RotateCameraAxis;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* TurnCameraLeft;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* TurnCameraRight;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* MoveCameraYAxis;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* MoveCameraXAxis;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* DragCamera;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* ZoomCamera;

protected:
	virtual void BeginPlay() override;

	void OnZoomCamera(const FInputActionValue& Value);
	void OnRotateCamera(const FInputActionValue& Value);
	void OnTurnCameraLeft(const FInputActionValue& Value);
	void OnTurnCameraRight(const FInputActionValue& Value);
	void OnMoveCameraYAxis(const FInputActionValue& Value);
	void OnMoveCameraXAxis(const FInputActionValue& Value);
	void OnDragCamera(const FInputActionValue& Value);

	void RequestMoveCamera(float X, float Y, float Scale);
	void ApplyMoveCameraCommands();

	/** @brief 相机所属的 Actor 引用 */
	UPROPERTY()
	AActor* CameraOwner;

	/** @brief 根组件场景引用，用于控制相机在世界中的平移和旋转 */
	UPROPERTY()
	USceneComponent* RootComponent;

	/** @brief 相机组件引用，提供 FOV 等光学参数 */
	UPROPERTY()
	UCameraComponent* CameraComponent;

	/** @brief 弹簧臂组件引用，用于控制相机的距离（臂长）和俯角 */
	UPROPERTY()
	USpringArmComponent* SpringArmComponent;

	/** @brief 玩家控制器引用，用于处理输入和视图设置 */
	UPROPERTY()
	APlayerController* RTSPlayerController;

	/** @brief 相机移动边界体积，用于限制相机活动范围 */
	UPROPERTY()
	AActor* BoundaryVolume;
	UPROPERTY()
	float DesiredZoomLength;

private:
	void CollectComponentDependencyReferences();
	void ConfigureSpringArm();

	void TryToFindBoundaryVolumeReference();
	void ConditionallyEnableEdgeScrolling();
	void CheckForEnhancedInputComponent();
	void BindInputMappingContext();
	void BindInputActions();

	void ConditionallyPerformEdgeScrolling();
	void EdgeScrollLeft();
	void EdgeScrollRight();
	void EdgeScrollUp();
	void EdgeScrollDown();

	void FollowTargetIfSet();
	void SmoothTargetArmLengthToDesiredZoom();
	void ConditionallyKeepCameraAtDesiredZoomAboveGround();
	void ConditionallyApplyCameraBounds();

	UPROPERTY()
	FName CameraBlockingVolumeTag;
	UPROPERTY()
	AActor* CameraFollowTarget;
	UPROPERTY()
	float DeltaSeconds;
	UPROPERTY()
	bool IsCameraOutOfBoundsErrorAlreadyDisplayed;
	UPROPERTY()
	bool IsDragging;
	UPROPERTY()
	FVector2D DragStartLocation;
	/** @brief 相机移动指令队列 */
	UPROPERTY()
	TArray<FMoveCameraCommand> MoveCameraCommands;

	/** @brief 当前移动速度 */
	UPROPERTY()
	float CurrentMovementSpeed;

public:
	/**
	 * @brief       计算并存储相机在地平面的四个视野投影点
	 **/
	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera|Minimap")
	TArray<FVector> MinimapFrustumPoints;

	/**
	 * @brief       强制更新视野框坐标。内部计算优先使用 DesiredZoomLength 以实现意图同步。
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera|Minimap")
	void UpdateMinimapFrustum();
};
