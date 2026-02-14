// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "InputMappingContext.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "RTSCamera.generated.h"

/** @brief 视野框数据更新时的多播委托声明 */
DECLARE_MULTICAST_DELEGATE(FOnMinimapFrustumUpdated);

/**
 * @brief       封装相机平移请求的指令结构
 **/
USTRUCT()
struct FMoveCameraCommand
{
	GENERATED_BODY()
	
	/// 目标位置在 X 轴上的分量增量
	UPROPERTY()
	float x = 0;
	
	/// 目标位置在 Y 轴上的分量增量
	UPROPERTY()
	float y = 0;
	
	/// 本次移动指令的缩放权重比例
	UPROPERTY()
	float scale = 0;
};

/**
 * @brief       RTS 相机组件，处理视口平移、边缘滚动、意图缩放及视野投影逻辑。
 * 
 * 组件遵循“战略由人，战术由AI”的设计原则，旨在平衡操作的顺滑感与 UI 的即时反馈。
 **/
UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class OPENRTSCAMERA_API URTSCamera : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * @brief       初始化相机组件的默认属性与子对象引用
	 **/
	URTSCamera();

	/**
	 * @brief       主逻辑更新函数。驱动物理插值、地形校正及边界约束。
	 * 
	 * @param       参数名称: deltaTime                     数据类型:        float
	 * @param       参数名称: tickType                      数据类型:        ELevelTick
	 * @param       参数名称: thisTickFunction              数据类型:        FActorComponentTickFunction*
	 **/
	/**
	 * @brief       每帧更新相机状态。驱动物理插值、地形校正及边界约束。
	 * 
	 * @param       参数名称: DeltaTime                     数据类型:        float
	 * @param       参数名称: TickType                      数据类型:        ELevelTick
	 * @param       参数名称: ThisTickFunction              数据类型:        FActorComponentTickFunction*
	 **/
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction
	) override;

	/**
	 * @brief       使相机视野物理锁定并跟随指定的目标 Actor
	 * 
	 * @param       参数名称: target                         数据类型:        AActor*
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void followTarget(AActor* target);

	/**
	 * @brief       取消当前的相机目标跟随状态
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void unFollowTarget();

	/**
	 * @brief       将本地控制器的相机观察点设置为此组件的所有者
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void setActiveCamera();
	
	/**
	 * @brief       将相机组件瞬間移动至指定的 X/Y 地面坐标
	 * 
	 * @param       参数名称: position                       数据类型:        FVector
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void jumpTo(FVector position);

	/**
	 * @brief       获取当前用于相机移动约束的边界体积引用
	 * 
	 * @return      返回值类型:      AActor*
	 **/
	UFUNCTION(BlueprintPure, Category = "RTSCamera")
	AActor* getMovementBoundaryVolume() const { return movementBoundaryVolume; }

	/// 相机缩放的最小目标距离（最接近地面）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float minimumZoomLength;

	/// 相机缩放的最大目标距离（最高视野）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float maximumZoomLength;

	/// 缩放插值的补全速率（值越大，物理位置追赶意图的速度越快）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float zoomCatchupSpeed;

	/// 单次滚轮操作触发的缩放距离步长
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings")
	float zoomSpeed;

	/// 初始化时的相机俯仰角 (Pitch)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float startingPitchAngle;

	/// 初始化时的相机偏航角 (Yaw)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float startingYawAngle;

	/// 相机在最大缩放高度时的移动速度上限
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float maxMovementSpeed;

	/// 相机在最小缩放高度时的基础移动速度
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float minMovementSpeed;

	/// 输入控制下的水平旋转感官速度
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	float rotationSpeed;
	
	/// 相机拖拽操作在视口中的拉伸增量比例
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera",
		meta = (ClampMin = "0.0", ClampMax = "1.0")
	)
	float dragExtent;

	/// 启用相机位置移动的插值延迟
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	bool enableCameraLag;

	/// 启用相机视野旋转的插值延迟
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera")
	bool enableCameraRotationLag;

	/// 启用基于地形起伏动态修正相机根高度的功能
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	bool enableDynamicCameraHeight;

	/// 用于地形高度探测的碰撞通道类型
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	TEnumAsByte<ECollisionChannel> collisionChannel;

	/// 地形校正射线在向上/向下扫描时的最大探测距离
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Dynamic Camera Height Settings",
		meta=(EditCondition="enableDynamicCameraHeight")
	)
	float findGroundTraceLength;

	/// 启用鼠标触发视口边缘后的相机滚动
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Edge Scroll Settings")
	bool enableEdgeScrolling;

	/// 判定为边缘的屏幕占比阈值（百分比）
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Edge Scroll Settings",
		meta=(EditCondition="enableEdgeScrolling")
	)
	float distanceFromEdgeThreshold;

	/// 相机对应的增强输入上下文
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputMappingContext* inputMappingContext;

	/// 旋转控制输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* rotateCameraAxisAction;

	/// 向左步进旋转输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* turnCameraLeftAction;

	/// 向右步进旋转输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* turnCameraRightAction;

	/// 前后移动输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* moveCameraYAxisAction;

	/// 左右移动输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* moveCameraXAxisAction;

	/// 鼠标拖拽输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* dragCameraAction;

	/// 缩放输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* zoomCameraAction;

	/// 委托实例：当视野投影点数组被成功更新后触发
	FOnMinimapFrustumUpdated onMinimapFrustumUpdated;

protected:
	/**
	 * @brief       生命周期起始点：建立组件依赖与输入绑定
	 **/
	virtual void BeginPlay() override;

	/**
	 * @brief       响应增强输入事件。执行缩放并标记視野更新。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onZoomCameraActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行相机旋转并标记視野更新。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onRotateCameraActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行向左转向操作。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onTurnCameraLeftActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行向右转向操作。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onTurnCameraRightActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。请求纵向平移指令。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onMoveCameraYAxisActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。请求横向平移指令。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onMoveCameraXAxisActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。控制鼠标拖拽状态下的相机平移逻辑。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onDragCameraActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       将坐标移动意图转化为战术指令并加入执行队列
	 * 
	 * @param       参数名称: x                             数据类型:        float
	 * @param       参数名称: y                             数据类型:        float
	 * @param       参数名称: scale                         数据类型:        float
	 **/
	void requestCameraMovement(float x, float y, float scale);

	/**
	 * @brief       在一个逻辑帧内，分步执行指令队列中积压的所有平移指令
	 **/
	void applyAccumulatedMovementCommands();

	/// 组件所属的 Actor 引用，定义了相机的生命周期主体
	UPROPERTY()
	AActor* cameraOwner;

	/// 根场景组件，控制整个相机组在该坐标下的水平面移动
	UPROPERTY()
	USceneComponent* rootComponent;

	/// 相机组件，定义视野视野参数与最终渲染输出
	UPROPERTY()
	UCameraComponent* cameraComponent;

	/// 弹簧臂组件，控制相机与中心点之间的意图长度与俯仰关系
	UPROPERTY()
	USpringArmComponent* springArmComponent;

	/// 实时战略专用的玩家控制器，分发输入事件
	UPROPERTY()
	APlayerController* realTimeStrategyPlayerController;

	/// 用于限制相机在地图中活动范围的体积
	UPROPERTY()
	AActor* movementBoundaryVolume;

	/// 玩家输入的理想缩放目标高度，视野计算将优先同步此意图而非物理插值过程
	UPROPERTY()
	float desiredZoomLength;

private:
	void resolveComponentDependencyPointers();
	void setupInitialSpringArmState();

	void locateMapBoundaryVolumeByTag();
	void configureInputModeForEdgeScrolling();
	void validateEnhancedInputAvailability();
	void registerInputMappingContext();
	void bindActionCallbacks();

	void executeEdgeScrollingEvaluation();
	void performEdgeScrollLeft();
	void performEdgeScrollRight();
	void performEdgeScrollUp();
	void performEdgeScrollDown();

	void updateFollowPositionIfTargetActive();
	void handleTargetArmLengthInterpolation();
	void rectifyRootHeightFromTerrain();
	void enforceCameraMovementBounds();

	/// 检索边界体积时匹配的静态场景标签
	UPROPERTY()
	FName cameraBlockingVolumeTag;

	/// 相机当前正在锁定跟随的 Actor 实测对象
	UPROPERTY()
	AActor* activeCameraFollowTarget;

	/// 自上一帧以来的时间增量（秒）
	UPROPERTY()
	float deltaSeconds;

	/// 状态位：指示是否正在进行鼠标拖拽操作
	UPROPERTY()
	bool isDragging;

	/// 拖拽操作开始时的视口坐标缓存
	UPROPERTY()
	FVector2D dragInteractionInitialLocation;

	/// 移动指令队列，用于适配变动帧率下的平滑渲染
	UPROPERTY()
	TArray<FMoveCameraCommand> pendingMovementCommands;

	/// 当前瞬时计算的相机移动速度值
	UPROPERTY()
	float currentMovementSpeed;

public:
	/// 静态数组，存储由视野投影计算出的地平面四个接地区顶点。
	/// 顺序遵循：[0]左上, [1]右上, [2]右下, [3]左下。
	FVector minimapFrustumPoints[4];

	/**
	 * @brief       强制触发視野投影计算，基于当前位置及缩放意图刷新 minimapFrustumPoint 数组数据。
	 *              注意：仅在相机产生明确的战略意图变更（移动、缩放、跳转）时产生计算开销。
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera|Minimap")
	void updateMinimapFrustum();
};
