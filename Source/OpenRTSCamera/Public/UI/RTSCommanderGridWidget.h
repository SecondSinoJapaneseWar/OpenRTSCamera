// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/UniformGridPanel.h"
#include "Data/RTSCommandGridAsset.h"
#include "RTSCommandButtonWidget.h"
#include "RTSActiveGroupWidget.h" 
#include "RTSCommanderGridWidget.generated.h"

/**
 * The 3x5 Grid Container.
 * Manages 15 RTSCommandButtonWidgets.
 */
UCLASS()
class OPENRTSCAMERA_API URTSCommanderGridWidget : public URTSActiveGroupWidget
{
	GENERATED_BODY()

public:

	virtual void NativeConstruct() override;
	virtual void NativePreConstruct() override;
	virtual void SynchronizeProperties() override;

	// Override to update grid when active group changes
	virtual void OnSelectionUpdated(const FRTSSelectionView& View) override;

protected:

	// The Grid Panel to hold buttons
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> CommandGridPanel;

	// Class to spawn for each button. 
    // MUST be set to a valid WBP in the details panel for the grid to appear.
	UPROPERTY(EditAnywhere, Category = "RTS Grid")
	TSubclassOf<URTSCommandButtonWidget> ButtonParams;
    
    // Padding between buttons
    UPROPERTY(EditAnywhere, Category = "RTS Grid")
    FMargin SlotPadding = FMargin(4.0f);

    // Desired size for buttons (if enforced by logic, though usually WBP controls this)
    UPROPERTY(EditAnywhere, Category = "RTS Grid")
    FVector2D ButtonSize = FVector2D(128.0f, 128.0f);

	// Internal list of buttons (Keys = Index 0-14)
	UPROPERTY()
	TArray<TObjectPtr<URTSCommandButtonWidget>> GridButtons;

	// Populate the grid based on data
	void RefreshGrid(const TArray<URTSCommandButton*>& Buttons);

	// Generate the 15 empty slots on Init
	void InitGridSlots();

    // 统一网格填充逻辑（支持 PreferredIndex 与自动空位）
    void PopulateSparseButtons(URTSCommandGridAsset* Grid, TArray<URTSCommandButton*>& OutButtons);

	UFUNCTION()
	void OnGridButtonClicked(const FGameplayTag& CommandTag);
	
    /** 响应 Actor 内部触发的网格变更通知 */
    UFUNCTION()
    void OnActorGridChanged();

    /** 响应全局导航请求（瞬态子菜单） */
    UFUNCTION()
    void OnCommandNavigationRequested(URTSCommandGridAsset* NewGrid);
	
	// Command resolver: Find asset for given unit ID
	// For now, this will just use a hardcoded reference or basic logic until we add the Interface/Trait
	URTSCommandGridAsset* ResolveGridForUnit(const FString& UnitName);

    // 纯 Push (Set/Reset) 模型辅助函数
    UFUNCTION(BlueprintCallable, Category = "RTS Grid")
    void UpdateGrid(URTSCommandGridAsset* NewGrid);

    UFUNCTION(BlueprintCallable, Category = "RTS Grid")
    void RefreshVisuals();

    // Cache the active actor for context
    TWeakObjectPtr<AActor> ActiveActorPtr;

    /** 当前正在显示的网格资产 (托管状态) */
    UPROPERTY()
    TWeakObjectPtr<URTSCommandGridAsset> CurrentGridAsset;

    // 保存当前的视图数据，以便在点击子网格后刷新时复用
    FRTSSelectionView LastSelectionView;

	// Test Asset for debugging
	UPROPERTY(EditAnywhere, Category = "Debug")
	TObjectPtr<URTSCommandGridAsset> DebugGridAsset;

    // --- Shared Tooltip Logic ---
protected:
    UPROPERTY(EditAnywhere, Category = "UI")
    TSubclassOf<class URTSTooltipWidget> TooltipClass;

    // If true, tooltip stays at a fixed offset from the Grid instead of following mouse
    UPROPERTY(EditAnywhere, Category = "UI")
    bool bFixedTooltipAboveGrid = true;

    // Y-Offset from Grid top when bFixedTooltipAboveGrid is true
    UPROPERTY(EditAnywhere, Category = "UI")
    float TooltipYOffset = -20.0f;

    UPROPERTY()
    TObjectPtr<class URTSTooltipWidget> SharedTooltip;

public:
    // Called by child buttons
    void NotifyButtonHovered(URTSCommandButtonWidget* Btn, URTSCommandButton* Data);
    void NotifyButtonUnhovered(URTSCommandButtonWidget* Btn);

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
