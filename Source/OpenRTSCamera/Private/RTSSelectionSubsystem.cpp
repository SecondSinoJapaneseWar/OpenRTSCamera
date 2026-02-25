#include "RTSSelectionSubsystem.h"
#include "RTSSelectable.h"
#include "RTSCommandSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interfaces/RTSCommandInterface.h"
#include "LandmarkSubsystem.h"
#include "Data/RTSCommandGridAsset.h"
#include "Data/RTSCommandButton.h"
#include "GameplayTagsManager.h"
#include "Components/MassBattleAgentComponent.h"
#include "Fragments/SubType.h"

DEFINE_LOG_CATEGORY(LogORTSSelection);

void URTSSelectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

    if (ULocalPlayer* LP = GetLocalPlayer())
    {
        if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
        {
            SignalHub->OnCommandIssued.AddLambda([this](FGameplayTag Tag, AActor* Context)
            {
                this->IssueCommand(Tag);
            });
        }
    }

    // C++ Auto-Config Grid (Transitent)
    // If no grid is provided, we create a default one with Move, Attack, Stop, Hold, Patrol
    if (DefaultEntityGrid.IsNull())
    {
        UE_LOG(LogORTSSelection, Log, TEXT("Selection: Auto-configuring transient default grid."));
        URTSCommandGridAsset* TransientGrid = NewObject<URTSCommandGridAsset>(this, TEXT("TransientDefaultGrid"));
        
        auto AddGenericBtn = [&](FString TagName, FString Name, int32 Index) {
            URTSCommandButton* Btn = NewObject<URTSCommandButton>(TransientGrid);
            Btn->CommandTag = FGameplayTag::RequestGameplayTag(FName(*TagName));
            Btn->DisplayName = FText::FromString(Name);
            Btn->PreferredIndex = Index;
            TransientGrid->Buttons.Add(Btn);

            // Ensure tag is registered natively to prevent warnings
            UGameplayTagsManager::Get().AddNativeGameplayTag(FName(*TagName), FString::Printf(TEXT("Default command %s"), *Name));
        };

        static bool bTagsRegistered = false;
        if (!bTagsRegistered)
        {
            AddGenericBtn(TEXT("RTS.Command.Move"), TEXT("移动"), 0);
            AddGenericBtn(TEXT("RTS.Command.Attack"), TEXT("攻击"), 1);
            AddGenericBtn(TEXT("RTS.Command.Stop"), TEXT("停止"), 2);
            AddGenericBtn(TEXT("RTS.Command.Hold"), TEXT("保持"), 3);
            AddGenericBtn(TEXT("RTS.Command.Patrol"), TEXT("巡逻"), 4);
            bTagsRegistered = true;
        }

        DefaultEntityGrid = TransientGrid;
        DefaultGridNative = TransientGrid; // Keep it alive and accessible
    }
}

void URTSSelectionSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void URTSSelectionSubsystem::SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier)
{
    TArray<AActor*> FinalActors = InActors;
    TArray<FEntityHandle> FinalEntities = InEntities;

    // Strategic Resolution: Convert Actors to Entities if they are Proxies
    for (int32 i = FinalActors.Num() - 1; i >= 0; i--)
    {
        AActor* Actor = FinalActors[i];
        if (Actor)
        {
            if (UMassBattleAgentComponent* MassAgent = Actor->FindComponentByClass<UMassBattleAgentComponent>())
            {
                FEntityHandle ProxiedEntity = MassAgent->GetEntityHandle();
                if (ProxiedEntity.Index != 0)
                {
                    FinalEntities.AddUnique(ProxiedEntity);
                    FinalActors.RemoveAt(i);
                }
            }
        }
    }

	// 1. Update Internal State
	if (Modifier == ERTSSelectionModifier::Replace)
	{
		SelectedActors = FinalActors;
		SelectedEntities = FinalEntities;
	}
	else if (Modifier == ERTSSelectionModifier::Add)
	{
		for (AActor* Actor : FinalActors) SelectedActors.AddUnique(Actor);
		for (const FEntityHandle& Handle : FinalEntities) SelectedEntities.AddUnique(Handle);
	}
	else if (Modifier == ERTSSelectionModifier::Remove)
	{
		for (AActor* Actor : FinalActors) SelectedActors.Remove(Actor);
		for (const FEntityHandle& Handle : FinalEntities) SelectedEntities.Remove(Handle);
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
		if (SelectedActors.Num() > 0) View.SingleUnit = CreateUnitDataFromActor(SelectedActors[0]);
		else View.SingleUnit = CreateUnitDataFromEntity(SelectedEntities[0]);
        View.Items.Add(View.SingleUnit);
	}
	else if (TotalCount <= ListModeMaxCount)
	{
		View.Mode = ERTSSelectionMode::List;
		for (AActor* Actor : SelectedActors) View.Items.Add(CreateUnitDataFromActor(Actor));
		for (const FEntityHandle& Handle : SelectedEntities) View.Items.Add(CreateUnitDataFromEntity(Handle));
		// 按类型名排序，保证同类型单位连续显示
		View.Items.Sort([](const FRTSUnitData& A, const FRTSUnitData& B){ return A.Name < B.Name; });
	}
	else
	{
		View.Mode = ERTSSelectionMode::Summary;
		TMap<FString, FRTSUnitData> GroupMap;

		for (AActor* Actor : SelectedActors)
		{
			FRTSUnitData Data = CreateUnitDataFromActor(Actor);
			FRTSUnitData& Group = GroupMap.FindOrAdd(Data.Name);
			if (Group.Count == 0 || Group.Name.IsEmpty()) { Group = Data; Group.Count = 0; }
			Group.Count++;
		}

		for (const FEntityHandle& Handle : SelectedEntities)
		{
			FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
			FRTSUnitData& Group = GroupMap.FindOrAdd(Data.Name);
			if (Group.Count == 0 || Group.Name.IsEmpty()) { Group = Data; Group.Count = 0; }
			Group.Count++;
		}

		for (auto& Pair : GroupMap) View.Items.Add(Pair.Value);
		// Summary 模式：按类型名字母排序，保证 City1→City2→... MassUnit_SubType0→... 依次连续
		View.Items.Sort([](const FRTSUnitData& A, const FRTSUnitData& B){ return A.Name < B.Name; });
	}

	// --- Tab Cycling ---
	AvailableGroupKeys.Reset();
	for(const auto& Item : View.Items) AvailableGroupKeys.AddUnique(Item.Name);
	AvailableGroupKeys.Sort();

	if (CurrentGroupIndex >= AvailableGroupKeys.Num()) CurrentGroupIndex = 0;
	if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex)) View.ActiveGroupKey = AvailableGroupKeys[CurrentGroupIndex];

	OnSelectionChanged.Broadcast(View);

    // --- Grid Synchronization ---
    // 核心设计：ActiveGroupKey 就是 TypeName（如 "City1", "MassUnit_SubType0"）
    // 直接用它查 Grid 表，不绕路遍历实体句柄
    URTSCommandGridAsset* NewGrid = nullptr;
    const FString& ActiveKey = View.ActiveGroupKey;

    if (!ActiveKey.IsEmpty())
    {
        // 路径A: Actor 组 —— 在选中 Actor 里找 ActiveKey 对应的 Actor，取其 Grid
        for (AActor* Actor : SelectedActors)
        {
            if (Actor && Actor->GetClass()->GetDisplayNameText().ToString() == ActiveKey
                && Actor->Implements<URTSCommandInterface>())
            {
                NewGrid = IRTSCommandInterface::Execute_GetCommandGrid(Actor);
                break;
            }
        }

        // 路径B: Entity 组 —— ActiveKey 就是 TypeName（City1/MassUnit_SubType0/...）
        // 直接查 LandmarkSubsystem 的 TypeGridAssets 表
        if (!NewGrid)
        {
            UWorld* World = GetWorld();
            ULandmarkSubsystem* LandmarkSub = World ? World->GetSubsystem<ULandmarkSubsystem>() : nullptr;
            if (LandmarkSub)
            {
                NewGrid = LandmarkSub->GetGridByType(ActiveKey);
            }
        }
    }

    // 路径C: 兜底默认 Grid（士兵移动/攻击/停止）
    if (!NewGrid && !DefaultEntityGrid.IsNull() && (SelectedActors.Num() > 0 || SelectedEntities.Num() > 0))
    {
        NewGrid = DefaultEntityGrid.LoadSynchronous();
    }

    OnCommandNavigationRequested.Broadcast(NewGrid);
    UE_LOG(LogORTSSelection, Log, TEXT("Selection: Modifier=%d Actors=%d Entities=%d ActiveKey=%s Grid=>%s"),
        (int32)Modifier, SelectedActors.Num(), SelectedEntities.Num(),
        *ActiveKey, NewGrid ? *NewGrid->GetName() : TEXT("NULL"));
}


void URTSSelectionSubsystem::ClearSelection()
{
	SetSelectedUnits(TArray<AActor*>(), TArray<FEntityHandle>(), ERTSSelectionModifier::Replace);
}

void URTSSelectionSubsystem::CycleGroup()
{
	if (AvailableGroupKeys.Num() <= 1) return;

	CurrentGroupIndex++;
	if (CurrentGroupIndex >= AvailableGroupKeys.Num()) CurrentGroupIndex = 0;
	SetSelectedUnits(SelectedActors, SelectedEntities, ERTSSelectionModifier::Replace);
}

void URTSSelectionSubsystem::RemoveUnit(const FRTSUnitData& UnitData)
{
	TArray<AActor*> ActorsToRemove;
	TArray<FEntityHandle> EntitiesToRemove;

	if (UnitData.ActorPtr) ActorsToRemove.Add(UnitData.ActorPtr);
	else if (UnitData.EntityHandle.Index != 0) EntitiesToRemove.Add(UnitData.EntityHandle);
	else
	{
		for (AActor* Act : SelectedActors) if (Act && Act->GetClass()->GetDisplayNameText().ToString() == UnitData.Name) ActorsToRemove.Add(Act);
	}

	SetSelectedUnits(ActorsToRemove, EntitiesToRemove, ERTSSelectionModifier::Remove);
}

void URTSSelectionSubsystem::SelectGroup(const FString& GroupKey)
{
	TArray<AActor*> NewActors;
	TArray<FEntityHandle> NewEntities;

	for (AActor* Act : SelectedActors) if (Act && Act->GetClass()->GetDisplayNameText().ToString() == GroupKey) NewActors.Add(Act);
	for (const FEntityHandle& Handle : SelectedEntities) 
    {
        FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
        if (Data.Name == GroupKey) NewEntities.Add(Handle);
    }

	SetSelectedUnits(NewActors, NewEntities, ERTSSelectionModifier::Replace);
}

FRTSUnitData URTSSelectionSubsystem::CreateUnitDataFromActor(AActor* Actor)
{
	FRTSUnitData Data;
	if (Actor)
	{
		Data.Name = Actor->GetClass()->GetDisplayNameText().ToString(); 
		Data.ActorPtr = Actor;
		Data.bIsMassEntity = false;
		
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

    UWorld* World = GetWorld();
    if (!World) return Data;

    // 路径1: 城市实体 —— 从 LandmarkSubsystem 反查类型名（City1~City5）用于分组
    if (ULandmarkSubsystem* LandmarkSub = World->GetSubsystem<ULandmarkSubsystem>())
    {
        FString EntityType = LandmarkSub->FindTypeByEntity(Handle);
        if (!EntityType.IsEmpty())
        {
            Data.Name = EntityType; // "City1", "City2" ...
            return Data;
        }
    }

    // 路径2: 普通 Mass 单位 —— 读取 FSubType.Index 作为分组 Key
    if (UMassEntitySubsystem* MassSys = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = MassSys->GetMutableEntityManager();
        if (Handle.Index > 0)
        {
            FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
            if (EM.IsEntityActive(NativeHandle))
            {
                if (const FSubType* SubFrag = EM.GetFragmentDataPtr<FSubType>(NativeHandle))
                {
                    Data.Name = FString::Printf(TEXT("MassUnit_SubType%d"), SubFrag->Index);
                    return Data;
                }
            }
        }
    }

    Data.Name = TEXT("Mass Unit");
	return Data;
}


void URTSSelectionSubsystem::IssueCommand(FGameplayTag CommandTag)
{
    UE_LOG(LogTemp, Log, TEXT("RTSSelectionSubsystem: Command %s Issued to Current Selection."), *CommandTag.ToString());

    // 1. 发送给选中的 Actor
    for (AActor* Actor : SelectedActors)
    {
        if (Actor && Actor->Implements<URTSCommandInterface>())
        {
            IRTSCommandInterface::Execute_ExecuteCommand(Actor, CommandTag);
        }
    }

    // 2. 核心补完：发送给选中的 Mass 实体 —— 解决“点击城市按钮无效/按钮显示默认”问题
    // 在 Mass-centric 架构下，即便选中的是 Entity，也应将其关联的 Actor 作为中转执行命令
    if (SelectedEntities.Num() > 0)
    {
        UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
        ULandmarkSubsystem* LandmarkSub = GetWorld()->GetSubsystem<ULandmarkSubsystem>();
        
        if (MassSubsystem)
        {
            FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
            
            for (const FEntityHandle& Handle : SelectedEntities)
            {
                // 先检查 Index 有效性，避免 IsValidIndex(-1) 崩溃
                if (Handle.Index <= 0) continue;
                FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
                if (!EntityManager.IsEntityActive(NativeHandle)) continue;

                AActor* CommandExecutor = nullptr;

                // 尝试 A: 获取该实体的渲染 Actor (如果有的话，例如在高 LOD 模式下)
                if (FRendering* RenderFrag = EntityManager.GetFragmentDataPtr<FRendering>(Handle))
                {
                    CommandExecutor = RenderFrag->BindingActorPtr.Get();
                }

                // 尝试 B: fallback - 新架构中 Command Grid 由 ULandmarkSettings 配置，
                // 无需通过模板 Actor 分发，此路径暂留以备后续扩展
                // if (!CommandExecutor && LandmarkSub)
                // {
                //     FString EntityType = LandmarkSub->FindTypeByEntity(Handle);
                //     CommandExecutor = LandmarkSub->GetTemplateActorByType(EntityType); // 已废弃
                // }

                // 执行指令
                if (CommandExecutor && CommandExecutor->Implements<URTSCommandInterface>())
                {
                    IRTSCommandInterface::Execute_ExecuteCommand(CommandExecutor, CommandTag);
                }
            }
        }
    }

    RequestCommandRefresh();
}

AActor* URTSSelectionSubsystem::GetActiveActor() const
{
    if (SelectedActors.Num() == 0) return nullptr;
    if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex))
    {
        const FString& ActiveKey = AvailableGroupKeys[CurrentGroupIndex];
        for (AActor* Actor : SelectedActors)
        {
            if (Actor && Actor->GetClass()->GetDisplayNameText().ToString() == ActiveKey) return Actor;
        }
    }
    return SelectedActors[0];
}
