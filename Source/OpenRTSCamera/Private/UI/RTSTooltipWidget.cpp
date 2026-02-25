// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSTooltipWidget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"

void URTSTooltipWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Enforce Default Style if user hasn't customized it heavily
    // Note: FSlateFontInfo is needed. We'll just set Size for now, keeping typeface default.
    
    if (TitleText)
    {
        FSlateFontInfo Font = TitleText->GetFont();
        Font.Size = DefaultFontSize; 
        TitleText->SetFont(Font);
    }
    
    // RichText doesn't use simple "SetFont", it uses a TextStyleSet/DataTable.
    // But we can try setting the "DefaultTextStyle"
    if (DescriptionText)
    {
        // For RichText, dynamic style modification is harder without a tailored Decorator.
        // However, we can set the "DefaultTextStyle" font size if exposed, 
        // or we just trust the user to set it in UMG since RichText is complex.
        
        // Let's at least try to set the default text style if possible
        // FTextBlockStyle DefaultStyle = DescriptionText->GetDefaultTextStyle();
        // DefaultStyle.Font.Size = DefaultFontSize;
        // DescriptionText->SetDefaultTextStyle(DefaultStyle);
        
        // Actually, easiest way is to wrap it in a ScaleBox if we just want size, 
        // but let's leave RichText alone for now as it relies on DataTables (GlobalStyle).
        // User asked for "Roboto 32".
    }
}

void URTSTooltipWidget::UpdateTooltip(URTSCommandButton* Data)
{
	if (!Data) return;

	if (TitleText)
	{
		TitleText->SetText(Data->DisplayName);
        
        // Re-apply font size in case it reset (unlikely but safe)
        FSlateFontInfo Font = TitleText->GetFont();
        Font.Size = DefaultFontSize; 
        TitleText->SetFont(Font);
	}

	if (DescriptionText)
	{
		DescriptionText->SetText(Data->Description);
	}

    if (IconImage)
    {
        if (Data->Icon)
        {
            IconImage->SetBrushFromTexture(Data->Icon);
            IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            IconImage->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    
    // Cost logic
    if (CostText)
    {
        FString CostStr;
        if (Data->LowValueCost > 0)
        {
            CostStr += FString::Printf(TEXT("%d 资金"), Data->LowValueCost);
        }
        if (Data->HighValueCost > 0)
        {
            if (!CostStr.IsEmpty()) CostStr += TEXT(" / "); // Separator
            CostStr += FString::Printf(TEXT("%d 军需"), Data->HighValueCost);
        }
        
        CostText->SetText(FText::FromString(CostStr));
        
        // Hide if free
        CostText->SetVisibility(CostStr.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        
        // Force font? Or rely on UMG
        // FSlateFontInfo CostFont = CostText->GetFont();
        // CostFont.Size = 24; // Smaller than title
        // CostText->SetFont(CostFont);
    }

	// Logic for Cost/Cooldown/etc. would go here if we had that data in DataAsset
	// For now we just show title and desc.
}
