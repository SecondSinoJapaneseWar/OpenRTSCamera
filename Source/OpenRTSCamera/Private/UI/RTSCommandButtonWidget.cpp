// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSCommandButtonWidget.h"
#include "UI/RTSTooltipWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Interfaces/RTSCommandInterface.h"
#include "UI/RTSCommanderGridWidget.h"

void URTSCommandButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (MainButton)
	{
		MainButton->OnClicked.AddDynamic(this, &URTSCommandButtonWidget::HandleClicked);
	}
}

void URTSCommandButtonWidget::Init(URTSCommandButton* InData, AActor* InContext, FKey InOverrideHotkey)
{
    ButtonData = InData;
    ContextActor = InContext;

    if (ButtonData)
    {
        UE_LOG(LogTemp, Log, TEXT("Button Init: %s (Tag: %s)"), *ButtonData->DisplayName.ToString(), *ButtonData->CommandTag.ToString());

        // Set Icon
        if (IconImage)
        {
            if (ButtonData->Icon)
            {
                IconImage->SetBrushFromTexture(ButtonData->Icon);
            }
            IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }

        // Set Hotkey Display
        if (HotkeyText)
        {
            FKey TargetKey = InOverrideHotkey.IsValid() ? InOverrideHotkey : ButtonData->Hotkey;
            
            // Check if key is valid
            if (!TargetKey.IsValid())
            {
                HotkeyText->SetVisibility(ESlateVisibility::Collapsed);
            }
            else
            {
                HotkeyText->SetText(TargetKey.GetDisplayName());
                HotkeyText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }

        // Reset State
        bIsCooldownActive = false;
        if (CooldownImage)
        {
            CooldownImage->SetVisibility(ESlateVisibility::Hidden);
            if (!CooldownMaterial)
            {
                CooldownMaterial = CooldownImage->GetDynamicMaterial();
            }
        }

        if (AutoCastBorder)
        {
            AutoCastBorder->SetVisibility(ESlateVisibility::Hidden);
        }

        SetVisibility(ESlateVisibility::Visible);
        
        // Remove Standard Tooltip to allow shared logic
        if (MainButton)
        {
            MainButton->SetToolTip(nullptr);
            if (!MainButton->OnHovered.IsAlreadyBound(this, &URTSCommandButtonWidget::HandleHovered))
                MainButton->OnHovered.AddDynamic(this, &URTSCommandButtonWidget::HandleHovered);
            if (!MainButton->OnUnhovered.IsAlreadyBound(this, &URTSCommandButtonWidget::HandleUnhovered))
                MainButton->OnUnhovered.AddDynamic(this, &URTSCommandButtonWidget::HandleUnhovered);
        }
    }
    else
    {
        // Null data means empty slot
        if (MainButton) MainButton->SetToolTip(nullptr);
        SetVisibility(ESlateVisibility::Hidden);
    }
}

void URTSCommandButtonWidget::HandleHovered()
{
    // Notify Parent Grid
    if (GetOuter())
    {
        if (URTSCommanderGridWidget* Grid = GetTypedOuter<URTSCommanderGridWidget>())
        {
            Grid->NotifyButtonHovered(this, ButtonData);
        }
    }
}

void URTSCommandButtonWidget::HandleUnhovered()
{
    if (GetOuter())
    {
        if (URTSCommanderGridWidget* Grid = GetTypedOuter<URTSCommanderGridWidget>())
        {
            Grid->NotifyButtonUnhovered(this);
        }
    }
}

void URTSCommandButtonWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
Super::NativeTick(MyGeometry, InDeltaTime);

// Update Availability, Cooldown & AutoCast State from Context
if (ButtonData && ContextActor.IsValid() && ContextActor->Implements<URTSCommandInterface>())
{
    // 0. Availability & Visibility logic (Scheme A)
    bool bAvailable = IRTSCommandInterface::Execute_IsCommandAvailable(ContextActor.Get(), ButtonData->CommandTag);
    
    if (!bAvailable)
    {
        if (ButtonData->bHideIfUnavailable)
        {
            SetVisibility(ESlateVisibility::Collapsed);
        }
        else
        {
            SetVisibility(ESlateVisibility::Visible);
            SetIsDisabled(true);
        }
    }
    else
    {
        SetVisibility(ESlateVisibility::Visible);
        SetIsDisabled(false);
    }

    // 1. Cooldown Logic
    float Remaining = IRTSCommandInterface::Execute_GetCooldownRemaining(ContextActor.Get(), ButtonData->CommandTag);
    bool bCurrentlyCooling = Remaining > 0.0f;

    // Edge Detection: Cooldown Started (or Widget just initialized on active CD)
    if (bCurrentlyCooling && !bIsCooldownActive)
    {
        if (CooldownMaterial && ButtonData->DefaultCooldown > 0.1f)
        {
            // Optional: Send Total Duration if needed for other effects (like shimmer speed)
             CooldownMaterial->SetScalarParameterValue(FName("CD_TotalDuration"), ButtonData->DefaultCooldown);
        }

        if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    // Edge Detection: Cooldown Ended
    else if (!bCurrentlyCooling && bIsCooldownActive)
    {
        if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::Hidden);
    }

    if (bCurrentlyCooling && CooldownMaterial)
    {
        // Calculate Phase (0.0 to 1.0)
        float Total = FMath::Max(ButtonData->DefaultCooldown, 0.001f);
        float Phase = FMath::Clamp(Remaining / Total, 0.0f, 1.0f);
        
        // Protocol Change: Send normalized "CD_Phase"
        CooldownMaterial->SetScalarParameterValue(FName("CD_Phase"), Phase);
    }

    bIsCooldownActive = bCurrentlyCooling;


        // 2. Auto-Cast
        if (ButtonData->bAllowAutoCast && AutoCastBorder)
        {
             bool bEnabled = IRTSCommandInterface::Execute_IsAutoCastEnabled(ContextActor.Get(), ButtonData->CommandTag);
             // Flash or Show
             AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        }
    }
}

FReply URTSCommandButtonWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // Handle Right Click for Auto-Cast
    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        if (ButtonData && ButtonData->bAllowAutoCast && ContextActor.IsValid() && ContextActor->Implements<URTSCommandInterface>())
        {
            IRTSCommandInterface::Execute_ToggleAutoCast(ContextActor.Get(), ButtonData->CommandTag);
            return FReply::Handled();
        }
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void URTSCommandButtonWidget::SetIsDisabled(bool bDisabled)
{
	if (MainButton)
	{
		MainButton->SetIsEnabled(!bDisabled);
	}
}

void URTSCommandButtonWidget::HandleClicked()
{
	if (ButtonData)
	{
        UE_LOG(LogTemp, Verbose, TEXT("RTSCommandButtonWidget: Clicked %s"), *ButtonData->CommandTag.ToString());
		OnCommandClicked.Broadcast(ButtonData->CommandTag);
	}
}
