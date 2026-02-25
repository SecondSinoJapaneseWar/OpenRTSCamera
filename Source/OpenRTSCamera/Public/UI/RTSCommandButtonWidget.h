// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/RTSCommandButton.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "RTSCommandButtonWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommandButtonClicked, const FGameplayTag&, CommandTag);

/**
 * A Single Button in the Command Grid.
 * Displays Icon, handles clicks, shows tooltip.
 */
UCLASS()
class OPENRTSCAMERA_API URTSCommandButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	
	virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION(BlueprintCallable, Category = "RTS Command")
	void Init(URTSCommandButton* InData, AActor* InContext = nullptr, FKey InOverrideHotkey = FKey());

    /** Returns the underlying data asset for this button. */
    UFUNCTION(BlueprintCallable, Category = "RTS Command")
    URTSCommandButton* GetData() const { return ButtonData; }

	UFUNCTION(BlueprintCallable, Category = "RTS Command")
	void SetIsDisabled(bool bDisabled);

	// Event for click
	UPROPERTY(BlueprintAssignable, Category = "RTS Command")
	FOnCommandButtonClicked OnCommandClicked;

protected:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MainButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

    // Cooldown Overlay (Image with Dynamic Material)
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> CooldownImage;

    // Hotkey Display
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<class UTextBlock> HotkeyText;

    // Auto-Cast Border (Image or Border)
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> AutoCastBorder;

	// The data asset backing this button
	UPROPERTY()
	TObjectPtr<URTSCommandButton> ButtonData;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> CooldownMaterial;

    // State tracking for efficient updates
    bool bIsCooldownActive = false;

    // The context actor (to query state)
    UPROPERTY()
    TWeakObjectPtr<AActor> ContextActor;

    // The class to use for tooltips - MOVED TO GRID
    // UPROPERTY(EditAnywhere, Category = "UI")
    // TSubclassOf<class URTSTooltipWidget> TooltipWidgetClass;

	UFUNCTION()
	void HandleClicked();
    
    UFUNCTION()
    void HandleHovered();

    UFUNCTION()
    void HandleUnhovered();
};
