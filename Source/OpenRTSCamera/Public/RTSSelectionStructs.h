// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassAPIStructs.h"
#include "RTSSelectionStructs.generated.h"

UENUM(BlueprintType)
enum class ERTSSelectionMode : uint8
{
	Single      UMETA(DisplayName = "Single Unit"),
	List        UMETA(DisplayName = "Unit List"), // < 12 units
	Summary     UMETA(DisplayName = "Group Summary") // > 12 units
};

UENUM(BlueprintType)
enum class ERTSSelectionModifier : uint8
{
	Replace     UMETA(DisplayName = "Replace Selection"),
	Add         UMETA(DisplayName = "Add to Selection"),
	Remove      UMETA(DisplayName = "Remove from Selection")
};

/**
 * Unified data structure representing a single selectable unit OR a group summary.
 */
USTRUCT(BlueprintType)
struct FRTSUnitData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	UTexture2D* Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	int32 Count = 1; // 1 for individual unit, >1 for group summary

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Health = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Energy = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxEnergy = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Shield = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxShield = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	bool bIsMassEntity = false;

	// Optional: Raw pointers/handles if UI needs to command them back
	// Only valid if Count == 1
	UPROPERTY()
	AActor* ActorPtr = nullptr;

	UPROPERTY()
	FEntityHandle EntityHandle;


	// Default constructor for "Empty/Unknown" state
	FRTSUnitData()
	{
		Name = TEXT("Unknown");
	}
};

/**
 * The snapshot sent to the UI.
 */
USTRUCT(BlueprintType)
struct FRTSSelectionView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	ERTSSelectionMode Mode = ERTSSelectionMode::Single;


	// Used when Mode == Single. Contains detailed info.
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FRTSUnitData SingleUnit;

	// Used when Mode == List (Individual items, Count=1) OR Summary (Grouped items, Count>1)
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TArray<FRTSUnitData> Items;

	// The key of the currently active sub-group (e.g. "Marine")
	// Used for highlighting and tab-cycling
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString ActiveGroupKey;
};
