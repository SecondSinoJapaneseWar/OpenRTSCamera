// Copyright 2024 Winy unq All Rights Reserved.

#include "RTSSelectionSubsystem.h"
#include "RTSSelectable.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void URTSSelectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void URTSSelectionSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

// Note: In header, bKeepIndex defaults to false. In CPP, we don't repeat the default value.
void URTSSelectionSubsystem::SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier)
{
	// 1. Update Internal State based on Modifier
	if (Modifier == ERTSSelectionModifier::Replace)
	{
		SelectedActors = InActors;
		SelectedEntities = InEntities;
	}
	else if (Modifier == ERTSSelectionModifier::Add)
	{
		// Append Actors (Unique)
		for (AActor* Actor : InActors)
		{
			SelectedActors.AddUnique(Actor);
		}
		
		// Append Entities (Unique)
		for (const FEntityHandle& Handle : InEntities)
		{
			SelectedEntities.AddUnique(Handle);
		}
	}
	else if (Modifier == ERTSSelectionModifier::Remove)
	{
		// Remove Actors
		for (AActor* Actor : InActors)
		{
			SelectedActors.Remove(Actor);
		}

		// Remove Entities
		for (const FEntityHandle& Handle : InEntities)
		{
			SelectedEntities.Remove(Handle);
		}
	}

	// 2. Generate View Data
	FRTSSelectionView View;
	int32 TotalCount = SelectedActors.Num() + SelectedEntities.Num();

	if (TotalCount == 0)
	{
		View.Mode = ERTSSelectionMode::Single;
	}
	else if (TotalCount == 1)
	{
		View.Mode = ERTSSelectionMode::Single;
		if (SelectedActors.Num() > 0)
		{
			View.SingleUnit = CreateUnitDataFromActor(SelectedActors[0]);
			View.Items.Add(View.SingleUnit);
		}
		else
		{
			View.SingleUnit = CreateUnitDataFromEntity(SelectedEntities[0]);
			View.Items.Add(View.SingleUnit);
		}
	}
	else if (TotalCount <= ListModeMaxCount)
	{
		View.Mode = ERTSSelectionMode::List;
		
		for (AActor* Actor : SelectedActors)
		{
			View.Items.Add(CreateUnitDataFromActor(Actor));
		}
		
		for (const FEntityHandle& Handle : SelectedEntities)
		{
			View.Items.Add(CreateUnitDataFromEntity(Handle));
		}
	}
	else
	{
		View.Mode = ERTSSelectionMode::Summary;
		
		// Map for grouping: TypeName -> GroupData
		TMap<FString, FRTSUnitData> GroupMap;

		for (AActor* Actor : SelectedActors)
		{
			FRTSUnitData Data = CreateUnitDataFromActor(Actor);
			FRTSUnitData& Group = GroupMap.FindOrAdd(Data.Name);
			if (Group.Count == 0 || Group.Name.IsEmpty())
			{
				Group = Data;
				Group.Count = 0;
			}
			Group.Count++;
		}

		for (const FEntityHandle& Handle : SelectedEntities)
		{
			FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
			FRTSUnitData& Group = GroupMap.FindOrAdd(Data.Name);
			if (Group.Count == 0 || Group.Name.IsEmpty())
			{
				Group = Data;
				Group.Count = 0;
			}
			Group.Count++;
		}

		for (auto& Pair : GroupMap)
		{
			View.Items.Add(Pair.Value);
		}
	}

	// --- Tab Cycling Logic ---
	AvailableGroupKeys.Reset();
	for(const auto& Item : View.Items)
	{
		AvailableGroupKeys.AddUnique(Item.Name);
	}
	AvailableGroupKeys.Sort();

	// Validate CurrentGroupIndex
	if (CurrentGroupIndex >= AvailableGroupKeys.Num()) 
	{
		CurrentGroupIndex = 0;
	}

	if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex))
	{
		View.ActiveGroupKey = AvailableGroupKeys[CurrentGroupIndex];
	}

	// Broadcast
	OnSelectionChanged.Broadcast(View);
}

void URTSSelectionSubsystem::ClearSelection()
{
	SetSelectedUnits(TArray<AActor*>(), TArray<FEntityHandle>());
}

// --- New Starcraft-style Controls ---

void URTSSelectionSubsystem::CycleGroup()
{
	if (AvailableGroupKeys.Num() <= 1) return;

	CurrentGroupIndex++;
	if (CurrentGroupIndex >= AvailableGroupKeys.Num())
	{
		CurrentGroupIndex = 0;
	}

	// Force Re-Generate View (Copying logic from SetSelectedUnits Step 2 for now)
	// ideally this should be a helper function
	
	// Re-run SetSelectedUnits with "Replace" effectively regenerates view, but it's expensive to copy arrays.
	// But since we can't easily add private methods to header without re-viewing, 
	// we will trigger a self-refresh using current selection.
	SetSelectedUnits(SelectedActors, SelectedEntities, ERTSSelectionModifier::Replace);
}

void URTSSelectionSubsystem::RemoveUnit(const FRTSUnitData& UnitData)
{
	// 1. Identify what to remove
	TArray<AActor*> ActorsToRemove;
	TArray<FEntityHandle> EntitiesToRemove;

	// Check if this is a "Group" removal (Summary Mode) or "Single" removal
	// In our data, if Element is from Summary, it might represent multiple.
	// But FRTSUnitData passed from UI is just a copy.
	// We check if we are in Summary Mode? 
	// Or simplistic approach: If UnitData has valid pointers, remove specific. 
	// If UnitData is a Summary representative (pointers might be null or first-of-group),
	// we might need to remove by Name.
	
	bool bIsGroupRemoval = false;
	// Check ID match in SelectedActors.
	
	if (UnitData.ActorPtr)
	{
		ActorsToRemove.Add(UnitData.ActorPtr);
	}
	else if (UnitData.EntityHandle.Index > 0)
	{
		EntitiesToRemove.Add(UnitData.EntityHandle);
	}
	else
	{
		// Fallback: Remove by Name (Type) if pointers missing (unlikely in List, possible in Summary?)
		// Actually, let's enable "Remove Type" if user held Shift on a Summary Icon.
		// For now, assume List Mode (Starcraft Wireframe).
		// If Summary Mode, we should probably iterate all Selected and remove matching Name.
		bIsGroupRemoval = true;
	}

	if (bIsGroupRemoval)
	{
		// Iterate and collect all matching Name
		for (AActor* Act : SelectedActors)
		{
			if (Act && Act->GetName() == UnitData.Name) ActorsToRemove.Add(Act); // Name match might need refinement
		}
		// Entities...
	}

	SetSelectedUnits(ActorsToRemove, EntitiesToRemove, ERTSSelectionModifier::Remove);
}

void URTSSelectionSubsystem::SelectGroup(const FString& GroupKey)
{
	// Filter Current Selection to ONLY keep this GroupKey
	TArray<AActor*> NewActors;
	TArray<FEntityHandle> NewEntities;

	// Filter Actors
	for (AActor* Act : SelectedActors)
	{
		if (Act)
		{
			// We need to match the key logic used in CreateUnitData
			FRTSUnitData Data = CreateUnitDataFromActor(Act);
			if (Data.Name == GroupKey)
			{
				NewActors.Add(Act);
			}
		}
	}

	// Filter Entities
	for (const FEntityHandle& Handle : SelectedEntities)
	{
		FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
		if (Data.Name == GroupKey)
		{
			NewEntities.Add(Handle);
		}
	}

	// Apply as Replace
	SetSelectedUnits(NewActors, NewEntities, ERTSSelectionModifier::Replace);
}

FRTSUnitData URTSSelectionSubsystem::CreateUnitDataFromActor(AActor* Actor)
{
	FRTSUnitData Data;
	if (Actor)
	{
		// Group by Class Name (Type) instead of Instance Name (Label)
		Data.Name = Actor->GetClass()->GetDisplayNameText().ToString(); 
		// Or GetName() + Remove _C? DisplayName is usually cleaner for UI.
		
		Data.ActorPtr = Actor;
		Data.bIsMassEntity = false;
		
		// Retrieve Data from Component
		if (auto Selectable = Actor->FindComponentByClass<URTSSelectable>())
		{
			Data.Icon = Selectable->Icon;
			Data.Health = Selectable->Health;
			Data.MaxHealth = Selectable->MaxHealth;
			Data.Energy = Selectable->Energy;
			Data.MaxEnergy = Selectable->MaxEnergy;
			Data.Shield = Selectable->Shield;
			Data.MaxShield = Selectable->MaxShield;
		}
	}
	return Data;
}

FRTSUnitData URTSSelectionSubsystem::CreateUnitDataFromEntity(const FEntityHandle& Handle)
{
	FRTSUnitData Data;
	Data.bIsMassEntity = true;
	Data.EntityHandle = Handle;

	// Default fallback
	Data.Name = TEXT("Mass Unit");

	// TODO: Use MassEntityManager to get fragments (Icon, Health, etc.)
	// UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	// if (MassSubsystem) { FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager(); ... }

	return Data;
}
