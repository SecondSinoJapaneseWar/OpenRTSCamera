// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RTSSelectionStructs.h"
#include "MassEntityTypes.h"
#include "MassAPIStructs.h"
#include "RTSSelectionSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogORTSSelection, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, const FRTSSelectionView&, SelectionView);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandRefreshRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommandNavigationRequested, class URTSCommandGridAsset*, NewGrid);

/**
 * Manages RTS selection state and formats data for the UI.
 */
UCLASS()
class OPENRTSCAMERA_API URTSSelectionSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
    /** 广播给 UI，请求刷新当前的指令网格（当单位内部状态改变时，如 CD 结束） */
    UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
    FOnCommandRefreshRequested OnCommandRefreshRequested;

    /** 广播给 UI，请求导航到一个特定的指令网格（瞬态导航，如打开子菜单） */
    UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
    FOnCommandNavigationRequested OnCommandNavigationRequested;

    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void RequestCommandRefresh() { OnCommandRefreshRequested.Broadcast(); }

    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void RequestGridNavigation(class URTSCommandGridAsset* NewGrid) { OnCommandNavigationRequested.Broadcast(NewGrid); }
	// Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	/**
	 * Clears current selection.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void ClearSelection();

    /** 
     * Fallback Command Grid used when an entity (like a soldier) does not have its own grid.
     */
    UPROPERTY(EditAnywhere, Category = "RTS Selection")
    TSoftObjectPtr<class URTSCommandGridAsset> DefaultEntityGrid;

	/**
	 * Cycles focus to the next available sub-group.
	 * (Tab functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void CycleGroup();

	/**
	 * Removes a specific unit/group from the current selection.
	 * (Shift-Click UI functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void RemoveUnit(const FRTSUnitData& UnitData);

	/**
	 * Restricts selection to ONLY units of the specified group key.
	 * (Ctrl-Click UI functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SelectGroup(const FString& GroupKey);

    /**
     * Issues an instant command to all selected units.
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommand(FGameplayTag CommandTag);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedActors() const { return SelectedActors.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedMass() const { return SelectedEntities.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsActorSelected(const AActor* Actor) const { return SelectedActors.Contains(Actor); }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsEntitySelected(const FEntityHandle& Handle) const { return SelectedEntities.Contains(Handle); }

    /** 返回当前选中的 Actor 列表 (只读访问) */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }

    /** 
     * 获取当前“激活”的 Actor (即当前选中组的代表)
     * 在 Direct Callback 模式下，它作为按钮执行的主要上下文。
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    AActor* GetActiveActor() const;

	/**
	 * Event fired when selection changes. UI should bind to this.
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
	FOnSelectionChanged OnSelectionChanged;

private:
	// Raw State
	UPROPERTY()
	TArray<AActor*> SelectedActors;

	TArray<FEntityHandle> SelectedEntities;

	// Cycle State
	UPROPERTY()
	TArray<FString> AvailableGroupKeys;
	
    UPROPERTY()
    TObjectPtr<class URTSCommandGridAsset> DefaultGridNative;

	int32 CurrentGroupIndex = 0;

	// Helpers
	FRTSUnitData CreateUnitDataFromActor(AActor* Actor);
	FRTSUnitData CreateUnitDataFromEntity(const FEntityHandle& Handle);

	// Thresholds
	const int32 ListModeMaxCount = 12;
};
