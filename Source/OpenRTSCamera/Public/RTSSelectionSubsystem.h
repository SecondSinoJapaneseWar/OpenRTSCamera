// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RTSSelectionStructs.h"
#include "MassEntityTypes.h"
#include "MassAPIStructs.h"
#include "RTSSelectionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, const FRTSSelectionView&, SelectionView);

/**
 * Manages RTS selection state and formats data for the UI.
 */
UCLASS()
class OPENRTSCAMERA_API URTSSelectionSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Main entry point for selection updates.
	 * Calculates the view mode (Single/List/Summary) and broadcasts OnSelectionChanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	/**
	 * Clears current selection.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void ClearSelection();

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

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedActors() const { return SelectedActors.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedMass() const { return SelectedEntities.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsActorSelected(const AActor* Actor) const { return SelectedActors.Contains(Actor); }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsEntitySelected(const FEntityHandle& Handle) const { return SelectedEntities.Contains(Handle); }

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
	
	int32 CurrentGroupIndex = 0;

	// Helpers
	FRTSUnitData CreateUnitDataFromActor(AActor* Actor);
	FRTSUnitData CreateUnitDataFromEntity(const FEntityHandle& Handle);

	// Thresholds
	const int32 ListModeMaxCount = 12;
};
