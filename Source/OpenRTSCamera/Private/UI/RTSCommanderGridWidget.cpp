#include "UI/RTSCommanderGridWidget.h"
#include "Components/UniformGridSlot.h"
#include "RTSSelectionSubsystem.h" 
#include "Interfaces/RTSCommandInterface.h" 


void URTSCommanderGridWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void URTSCommanderGridWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	InitGridSlots();
}

void URTSCommanderGridWidget::NativeConstruct()
{
	Super::NativeConstruct();

	InitGridSlots();
	
    // 绑定全局通知（模块间解耦的通信枢纽）
    if (UWorld* World = GetWorld())
    {
        if (ULocalPlayer* LP = World->GetFirstLocalPlayerFromController())
        {
            if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
            {
                Selection->OnCommandRefreshRequested.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnActorGridChanged);
                Selection->OnCommandNavigationRequested.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnCommandNavigationRequested);
                // 绑定选择变化通知，驱动 ActiveActorPtr 更新和 Grid 刷新
                Selection->OnSelectionChanged.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnSelectionUpdated);
            }

    // 监听低层级指令系统的导航请求 (二进制导航)
    if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
    {
        SignalHub->OnNavigationRequested.AddLambda([this](URTSCommandGridAsset* NewGrid, AActor* Context)
        {
            this->UpdateGrid(NewGrid);
        });
    }
        }
    }
	
	// If Debug Asset is set, load it immediately for testing
	if (DebugGridAsset)
	{
		// RefreshGrid(DebugGridAsset->GetAllButtons()); // Need better logic here for sparse array
	}
}

void URTSCommanderGridWidget::InitGridSlots()
{
	if (!CommandGridPanel)
    {
         UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: CommandGridPanel is NULL!"));
         return;
    }
    
    if (!ButtonParams)
    {
         UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: ButtonParams is NULL! Please assign a WBP_CommandButton class in the Widget Blueprint Details."));
         return;
    }

	CommandGridPanel->ClearChildren();
	GridButtons.Empty();

    CommandGridPanel->SetSlotPadding(SlotPadding);
    CommandGridPanel->SetMinDesiredSlotWidth(ButtonSize.X);
    CommandGridPanel->SetMinDesiredSlotHeight(ButtonSize.Y);

	// Create 15 slots (3 rows x 5 columns)
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 5; ++Col)
		{
			URTSCommandButtonWidget* Btn = CreateWidget<URTSCommandButtonWidget>(this, ButtonParams);
			if (Btn)
			{
				UUniformGridSlot* GridSlot = CommandGridPanel->AddChildToUniformGrid(Btn, Row, Col);
				if (GridSlot)
				{
					GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
					GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
				}

				if (!IsDesignTime())
                {
				    Btn->OnCommandClicked.AddDynamic(this, &URTSCommanderGridWidget::OnGridButtonClicked);
                }
				GridButtons.Add(Btn); // Index = Row*5 + Col
			}
		}
	}
}

void URTSCommanderGridWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	Super::OnSelectionUpdated(View);
    LastSelectionView = View;

	URTSCommandGridAsset* BaseGrid = nullptr;
    
    // --- 核心逻辑变更：基于类型（ActiveGroupKey）获取命令面板 ---
    if (ULandmarkSubsystem* LandmarkSys = GetWorld()->GetSubsystem<ULandmarkSubsystem>())
    {
        BaseGrid = LandmarkSys->GetGridByType(View.ActiveGroupKey);
    }

    // 如果 Subsystem 没找到映射，尝试从 ActiveActor 兜底（为了兼容非地标单位，如普通士兵）
    if (!BaseGrid)
    {
        if (ULocalPlayer* LP = GetOwningLocalPlayer())
        {
            if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
            {
                AActor* ActiveActor = Selection->GetActiveActor();
                if (ActiveActor && ActiveActor->Implements<URTSCommandInterface>())
                {
                    BaseGrid = IRTSCommandInterface::Execute_GetCommandGrid(ActiveActor);
                }
            }
        }
    }

    // 更新网格（UpdateGrid 内部会处理 BaseGrid 是否变化的逻辑）
    UpdateGrid(BaseGrid);
}

void URTSCommanderGridWidget::UpdateGrid(URTSCommandGridAsset* NewGrid)
{
    // 如果是 NULL，即执行 Reset 操作
    CurrentGridAsset = NewGrid;
    
    TArray<URTSCommandButton*> SparseList;
    SparseList.Init(nullptr, 15);
    if (NewGrid)
    {
        PopulateSparseButtons(NewGrid, SparseList);
    }
    
    RefreshGrid(SparseList);
    
    if (NewGrid)
    {
        UE_LOG(LogTemp, Log, TEXT("UI-Grid: Set Grid Asset: %s"), *NewGrid->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("UI-Grid: Grid Reset (Set NULL)"));
    }
}

void URTSCommanderGridWidget::RefreshVisuals()
{
    if (!CurrentGridAsset.IsValid()) return;

    const FKey GridKeys[] = {
        EKeys::Q, EKeys::W, EKeys::E, EKeys::R, EKeys::T,
        EKeys::A, EKeys::S, EKeys::D, EKeys::F, EKeys::G,
        EKeys::Z, EKeys::X, EKeys::C, EKeys::V, EKeys::B
    };

    TArray<URTSCommandButton*> SparseList;
    PopulateSparseButtons(CurrentGridAsset.Get(), SparseList);
    
    for (int32 i = 0; i < 15; ++i)
    {
        if (GridButtons.IsValidIndex(i) && GridButtons[i])
        {
            // 增量刷新时必须保留布局决定的快捷键（Q/W/E），否则会被重置为 None
            GridButtons[i]->Init(SparseList[i], ActiveActorPtr.Get(), GridKeys[i]);
        }
    }
    UE_LOG(LogTemp, Verbose, TEXT("UI-Grid: Visuals Refreshed."));
}

void URTSCommanderGridWidget::PopulateSparseButtons(URTSCommandGridAsset* Grid, TArray<URTSCommandButton*>& OutButtons)
{
    if (!Grid) return;
    OutButtons.Init(nullptr, 15);

    // 1. 获取所有按钮（支持虚函数重写，覆盖了单例面板和普通资产面板）
    TArray<URTSCommandButton*> AllButtons = Grid->GetAllButtons();

    // 2. 先尝试放入 PreferredIndex 位置
    TArray<URTSCommandButton*> Untracked;
    for (URTSCommandButton* Btn : AllButtons)
    {
        if (!Btn) continue;

        int32 Idx = Btn->PreferredIndex;
        if (Idx >= 0 && Idx < 15 && OutButtons[Idx] == nullptr)
        {
            OutButtons[Idx] = Btn;
        }
        else
        {
            Untracked.Add(Btn);
        }
    }

    // 3. 将没有固定位置（或位置冲突）的按钮放入空位
    int32 StartSearch = 0; 
    for (URTSCommandButton* Btn : Untracked)
    {
        for (int32 i = StartSearch; i < 15; ++i)
        {
            if (OutButtons[i] == nullptr)
            {
                OutButtons[i] = Btn;
                break;
            }
        }
    }
}

void URTSCommanderGridWidget::RefreshGrid(const TArray<URTSCommandButton*>& Buttons)
{
	if (Buttons.Num() != 15) return;

    const FKey GridKeys[] = {
        EKeys::Q, EKeys::W, EKeys::E, EKeys::R, EKeys::T,
        EKeys::A, EKeys::S, EKeys::D, EKeys::F, EKeys::G,
        EKeys::Z, EKeys::X, EKeys::C, EKeys::V, EKeys::B
    };

	for (int32 i = 0; i < 15; ++i)
	{
		if (GridButtons.IsValidIndex(i) && GridButtons[i])
		{
			GridButtons[i]->Init(Buttons[i], ActiveActorPtr.Get(), GridKeys[i]);
		}
	}
}

void URTSCommanderGridWidget::OnActorGridChanged()
{
    // 该函数现在映射为 RefreshVisuals 以保持向后兼容
    RefreshVisuals();
}

void URTSCommanderGridWidget::OnCommandNavigationRequested(URTSCommandGridAsset* NewGrid)
{
    // 直接从 Subsystem 拿当前激活 Actor，不走 ActiveActorPtr 中间状态
    if (ULocalPlayer* LP = GetOwningLocalPlayer())
    {
        if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
        {
            ActiveActorPtr = Selection->GetActiveActor();
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[Grid] Widget recv Navigation: Grid=%s Actor=%s"),
        NewGrid ? *NewGrid->GetName() : TEXT("NULL"),
        ActiveActorPtr.IsValid() ? *ActiveActorPtr->GetName() : TEXT("NULL"));
    UpdateGrid(NewGrid);
}

#include "Data/RTSCmd_SubMenu.h"

void URTSCommanderGridWidget::OnGridButtonClicked(const FGameplayTag& CommandTag)
{
    // 二进制核心：直接执行
    // 理由：虽然 UI 代理传回的是 Tag，但我们立即将其还原回 Button 对象，
    // 以便执行其包含完整 C++ 逻辑的回调函数（Execute），彻底废除“Actor 查找”链路。
    URTSCommandButton* ClickedData = nullptr;
    for (URTSCommandButtonWidget* BtnWidget : GridButtons)
    {
        if (BtnWidget && BtnWidget->GetVisibility() == ESlateVisibility::Visible)
        {
            if (URTSCommandButton* Data = BtnWidget->GetData())
            {
                if (Data->CommandTag.MatchesTagExact(CommandTag))
                {
                    ClickedData = Data;
                    break;
                }
            }
        }
    }

    if (!ClickedData) return;

    if (ULocalPlayer* LP = GetOwningLocalPlayer())
    {
        if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
        {
            // 对于非针对单位的逻辑（如顾问、科技），Actor 指针可能为空，
            // 但对于“兴奋剂”等单位技能，我们需要传入正确的执行者。
            AActor* ActiveActor = Selection->GetActiveActor();
            
            // 战术直达：按钮逻辑自决 (Pure Callback)
            // 基础按钮会发 Tag 给 Actor，子菜单按钮会命令 UI 导航。
            ClickedData->Execute(ActiveActor);
        }
    }
}

// --- Shared Tooltip Implementation ---

void URTSCommanderGridWidget::NotifyButtonHovered(URTSCommandButtonWidget* Btn, URTSCommandButton* Data)
{
    if (!Data) return;

    // Lazy Create
    if (!SharedTooltip && TooltipClass)
    {
        SharedTooltip = CreateWidget<URTSTooltipWidget>(GetOwningPlayer(), TooltipClass);
        if (SharedTooltip)
        {
            SharedTooltip->AddToViewport(100); // High Z-Order
            SharedTooltip->SetVisibility(ESlateVisibility::Collapsed);
            UE_LOG(LogTemp, Log, TEXT("Shared Tooltip Created."));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to create Shared Tooltip! Check TooltipClass is valid."));
        }
    }
    else if (!TooltipClass)
    {
         UE_LOG(LogTemp, Warning, TEXT("TooltipClass is NULL in RTSCommanderGridWidget! Please assign WBP_Tooltip."));
    }

    if (SharedTooltip)
    {
        SharedTooltip->UpdateTooltip(Data);
        SharedTooltip->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        UE_LOG(LogTemp, Verbose, TEXT("Showing Tooltip for: %s"), *Data->DisplayName.ToString());
    }
}

void URTSCommanderGridWidget::NotifyButtonUnhovered(URTSCommandButtonWidget* Btn)
{
    if (SharedTooltip)
    {
        SharedTooltip->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void URTSCommanderGridWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (SharedTooltip && SharedTooltip->GetVisibility() == ESlateVisibility::SelfHitTestInvisible)
    {
        if (bFixedTooltipAboveGrid)
        {
            // Position it above the CommandGridPanel
            // Use the geometry of this widget to find the screen pos
            FVector2D GridPos = MyGeometry.GetAbsolutePosition();
            
            FVector2D TooltipSize = SharedTooltip->GetDesiredSize();
            if (TooltipSize.IsZero()) TooltipSize = FVector2D(400.0f, 250.0f);

            // Left-Align with the Grid (Standard RTS Style)
            FVector2D FinalPos;
            FinalPos.X = GridPos.X; 
            FinalPos.Y = GridPos.Y - TooltipSize.Y + TooltipYOffset;

            SharedTooltip->SetPositionInViewport(FinalPos);
        }
        else
        {
            // Follow Mouse Logic (Existing)
            FVector2D MousePos;
            if (GetOwningPlayer() && GetOwningPlayer()->GetMousePosition(MousePos.X, MousePos.Y))
            {
                FVector2D ViewportSize;
                if (GEngine && GEngine->GameViewport)
                {
                    GEngine->GameViewport->GetViewportSize(ViewportSize);
                }
                
                FVector2D TooltipSize = SharedTooltip->GetDesiredSize();
                if (TooltipSize.IsZero()) TooltipSize = FVector2D(400.0f, 300.0f);

                FVector2D FinalPos = MousePos;
                
                if (MousePos.X > ViewportSize.X * 0.5f) FinalPos.X -= TooltipSize.X + 10.0f;
                else FinalPos.X += 40.0f;

                if (MousePos.Y > ViewportSize.Y * 0.5f) FinalPos.Y -= TooltipSize.Y + 10.0f;
                else FinalPos.Y += 40.0f;

                SharedTooltip->SetPositionInViewport(FinalPos);
            }
        }
    }
}
