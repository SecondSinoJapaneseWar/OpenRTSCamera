#pragma once
#include "RTSSelectable.generated.h"

UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class OPENRTSCAMERA_API URTSSelectable : public UActorComponent
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "RTS Selection")
	void OnSelected();

	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "RTS Selection")
	void OnDeselected();

	// --- Visual Data ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	UTexture2D* Icon;

	// --- Status Data (Standard RTS) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float Health = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float Energy = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float MaxEnergy = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float Shield = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Data")
	float MaxShield = 0.0f;
};
