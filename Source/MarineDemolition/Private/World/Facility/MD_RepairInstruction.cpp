#include "World/Facility/MD_RepairInstruction.h"
#include "World/Wall/MD_PowerConduit.h"
#include "World/Door/MD_DoorFaultComponent.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

AMD_RepairInstructionController::AMD_RepairInstructionController()
{
	bReplicates = true; bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Fault = CreateDefaultSubobject<UMD_FacilityFaultComponent>(TEXT("Fault"));
	Fault->Kind = EMD_FacilityKind::Breaker; Fault->bRandomFaultEligible = false;
	BreakerBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BreakerBody")); BreakerBody->SetupAttachment(RootComponent); BreakerBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMD_RepairInstructionController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMD_RepairInstructionController, State);
	DOREPLIFETIME(AMD_RepairInstructionController, Guide); DOREPLIFETIME(AMD_RepairInstructionController, Restart);
	DOREPLIFETIME(AMD_RepairInstructionController, Knobs); DOREPLIFETIME(AMD_RepairInstructionController, InstructionDoor);
}

bool AMD_RepairInstructionController::HasValidConfiguration() const
{
	if (!IsValid(Guide) || !IsValid(Restart) || Guide == Restart || Knobs.Num() != 4
		|| Guide->Controller != this || Restart->Controller != this
		|| Guide->ControlKind != EMD_InstructionControlKind::Guide || Restart->ControlKind != EMD_InstructionControlKind::Restart
		|| Guide->GetWorld() != GetWorld() || Restart->GetWorld() != GetWorld()
		|| !IsValid(InstructionRoom) || !IsValid(InstructionDoor)
		|| InstructionDoor->GetWorld() != GetWorld() || InstructionRoom->GetWorld() != GetWorld()) return false;
	const UFunction* Apply = InstructionDoor->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Apply || Apply->NumParms != 0 || !FindFProperty<FBoolProperty>(InstructionDoor->GetClass(),TEXT("bDoorOpen"))) return false;
	TSet<const AMD_RepairInstructionControl*> Unique;
	for (int32 I=0; I<4; ++I)
	{
		const AMD_RepairInstructionControl* Knob = Knobs[I];
		if (!IsValid(Knob) || Knob->Controller != this || Knob->ControlKind != EMD_InstructionControlKind::Knob
			|| Knob->Column != I || Knob->GetWorld() != GetWorld() || Unique.Contains(Knob)) return false;
		Unique.Add(Knob);
	}
	const UBoxComponent* Box = Cast<UBoxComponent>(InstructionRoom->GetCollisionComponent());
	return Box && Box->GetUnscaledBoxExtent().GetMin() > 0.f && Box->GetComponentScale().GetAbs().GetMin() > SMALL_NUMBER;
}

TArray<int32> AMD_RepairInstructionController::GenerateDifferentAnswer(const TArray<int32>& Previous, FRandomStream& Rng)
{
	TArray<int32> Result; for (int32 I=0; I<4; ++I) Result.Add(Rng.RandRange(0,4));
	// Never start solved at the top row, and never reuse the previous restart answer.
	if (Result == Previous || Result == TArray<int32>({0,0,0,0})) Result[0] = (Result[0]+1)%5;
	if (Result == Previous) Result[1] = (Result[1]+1)%5;
	return Result;
}

void AMD_RepairInstructionController::BeginPlay()
{
	Super::BeginPlay(); Random.Initialize(FMath::Rand());
	Fault->OnStateChanged.AddUniqueDynamic(this,&ThisClass::HandleOwnFault);
	if (HasAuthority())
	{
		if(IsValid(InstructionDoor) && InstructionDoor->GetWorld()==GetWorld())
		{ InstructionDoor->SetReplicates(true); InstructionDoor->FlushNetDormancy(); InstructionDoor->ForceNetUpdate(); }
		// Components can enter an initial fault before the actor's BeginPlay binds its delegate.
		if (Fault->IsFaultActive()) HandleOwnFault(Fault,FMD_FacilitySnapshot(),Fault->GetSnapshot());
		for (AMD_PowerConduit* Wire : PowerConduits)
			if (IsValid(Wire) && Wire->GetWorld()==GetWorld() && Wire->FacilityRepair)
				Wire->FacilityRepair->OnStateChanged.AddUniqueDynamic(this,&ThisClass::HandlePowerFault);
		TrackBrokenPowerSources();
		if (!SourceIncidents.IsEmpty()) StartRepairIncident();
	}
	OnRep_State();
}

void AMD_RepairInstructionController::EndPlay(const EEndPlayReason::Type Reason)
{
	for (AMD_PowerConduit* Wire : PowerConduits)
		if (IsValid(Wire) && Wire->FacilityRepair) Wire->FacilityRepair->OnStateChanged.RemoveDynamic(this,&ThisClass::HandlePowerFault);
	Fault->OnStateChanged.RemoveDynamic(this,&ThisClass::HandleOwnFault);
	ReleaseDoor(); SourceIncidents.Empty();
	Super::EndPlay(Reason);
}

void AMD_RepairInstructionController::TrackBrokenPowerSources()
{
	for (AMD_PowerConduit* Wire : PowerConduits)
		if (IsValid(Wire) && Wire->GetWorld()==GetWorld() && Wire->FacilityRepair && Wire->FacilityRepair->IsFaultActive())
			SourceIncidents.Add(Wire->FacilityRepair.Get(),Wire->FacilityRepair->GetSnapshot().FaultId);
}

void AMD_RepairInstructionController::HandlePowerFault(UMD_FacilityFaultComponent* Source, const FMD_FacilitySnapshot&, const FMD_FacilitySnapshot& Current)
{
	if (!HasAuthority() || bCompleting || !Source || !PowerConduits.Contains(Cast<AMD_PowerConduit>(Source->GetOwner()))) return;
	if (Current.IsFaultActive())
	{
		SourceIncidents.Add(Source,Current.FaultId);
		if (!State.bActive) StartRepairIncident();
	}
	else
	{
		SourceIncidents.Remove(Source);
		// An external tape repair stays valid. Do not leave readers locked after all linked power is restored.
		if (State.bActive && SourceIncidents.IsEmpty()) Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId);
	}
}

bool AMD_RepairInstructionController::StartRepairIncident()
{
	if (!HasAuthority() || State.bActive || !HasValidConfiguration()) return false;
	return Fault->TryBreak();
}

void AMD_RepairInstructionController::HandleOwnFault(UMD_FacilityFaultComponent*, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	if (!HasAuthority() || Previous.IsFaultActive()==Current.IsFaultActive()) return;
	State.bActive = Current.IsFaultActive(); State.bDoorLocked = false; State.bGuideRead = false;
	if (State.bActive)
	{
		const auto* Locked=IsValid(InstructionDoor)?FindFProperty<FBoolProperty>(InstructionDoor->GetClass(),TEXT("bLocked")):nullptr;
		State.bDoorPreviouslyLocked=Locked && Locked->GetPropertyValue_InContainer(InstructionDoor);
		State.Revision = State.Revision == MAX_int32 ? 1 : State.Revision+1;
		State.TargetRows = GenerateDifferentAnswer(State.TargetRows,Random); State.CurrentRows = {0,0,0,0};
		TrackBrokenPowerSources();
	}
	else SourceIncidents.Empty();
	PublishState();
}

int32 AMD_RepairInstructionController::GetParticipantCount() const
{
	int32 Count=0;
	if (const AGameStateBase* GS=GetWorld()?GetWorld()->GetGameState():nullptr)
		for (const APlayerState* PS:GS->PlayerArray)
			if (IsValid(PS) && !PS->IsInactive() && !PS->IsSpectator() && !PS->IsOnlyASpectator()) ++Count;
	return FMath::Clamp(Count,1,4);
}

bool AMD_RepairInstructionController::IsInInstructionRoom(const APawn* Pawn) const
{
	const UBoxComponent* Box=IsValid(InstructionRoom)?Cast<UBoxComponent>(InstructionRoom->GetCollisionComponent()):nullptr;
	if (!IsValid(Pawn) || !Box) return false;
	const FVector P=Box->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const FVector E=Box->GetUnscaledBoxExtent();
	return !P.ContainsNaN() && FMath::Abs(P.X)<=E.X && FMath::Abs(P.Y)<=E.Y && FMath::Abs(P.Z)<=E.Z;
}

bool AMD_RepairInstructionController::ApplyControl(AMD_RepairInstructionControl* Control, APawn* Pawn, int32 ExpectedRevision)
{
	const auto* Interaction=IsValid(Pawn)?Pawn->FindComponentByClass<UMD_InteractionComponent>():nullptr;
	const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr; const auto* PS=Pawn?Pawn->GetPlayerState():nullptr;
	if (!HasAuthority() || !State.bActive || !HasValidConfiguration() || ExpectedRevision!=State.Revision
		|| !IsValid(Control) || Control->Controller!=this || !IsValid(Pawn) || Pawn->GetWorld()!=GetWorld()
		|| !Pawn->GetController() || !Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding()
		|| !IsValid(PS) || !GS || !GS->PlayerArray.Contains(PS) || PS->IsInactive() || PS->IsSpectator() || PS->IsOnlyASpectator()) return false;
	if (Control==Guide || Control==Restart)
	{
		if (!IsInInstructionRoom(Pawn)) return false;
		if (Control==Guide) { State.bGuideRead=true; State.bDoorLocked=GetParticipantCount()>1; }
		else
		{
			State.TargetRows=GenerateDifferentAnswer(State.TargetRows,Random); State.CurrentRows={0,0,0,0};
			State.Revision=State.Revision==MAX_int32?1:State.Revision+1;
			State.bGuideRead=false; State.bDoorLocked=false;
		}
	}
	else
	{
		const int32 Column=Control->Column;
		if (!State.bGuideRead || !Knobs.IsValidIndex(Column) || Knobs[Column]!=Control || !State.CurrentRows.IsValidIndex(Column)) return false;
		State.CurrentRows[Column]=(State.CurrentRows[Column]+1)%5;
		if (State.CurrentRows==State.TargetRows) { FinishRepair(); return true; }
	}
	PublishState(); return true;
}

void AMD_RepairInstructionController::FinishRepair()
{
	const int32 Id=Fault->GetSnapshot().FaultId; bCompleting=true;
	const auto Sources=SourceIncidents;
	for (const auto& Pair:Sources)
		if (UMD_FacilityFaultComponent* Source=Pair.Key.Get())
			Source->CompleteRepairFromValidatedSource(Pair.Value);
	const bool bCompleted=Fault->CompleteRepairFromValidatedSource(Id); bCompleting=false;
	if (bCompleted) OnRepairCompleted.Broadcast(Id);
}

void AMD_RepairInstructionController::PublishState() { OnRep_State(); FlushNetDormancy(); ForceNetUpdate(); }

void AMD_RepairInstructionController::OnRep_State()
{
	ApplyDoorState();
	if (IsValid(Guide)) Guide->RefreshPresentation();
	if (IsValid(Restart)) Restart->RefreshPresentation();
	for (AMD_RepairInstructionControl* Knob:Knobs) if (IsValid(Knob)) Knob->RefreshPresentation();
}

void AMD_RepairInstructionController::ApplyDoorState()
{
	if (!State.bActive) { ReleaseDoor(); return; }
	AActor* Door=InstructionDoor;
	if (!IsValid(Door)) return;
	FBoolProperty* Open=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen"));
	FBoolProperty* Locked=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"));
	UFunction* Apply=Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Open || !Apply || Apply->NumParms!=0) return;
	if (!bDoorOwned)
	{
		ManagedDoor=Door; bDoorOwned=true; bPreviousLocked=State.bDoorPreviouslyLocked;
		for (auto* C:TInlineComponentArray<UPrimitiveComponent*>(Door))
			if (C->GetGenerateOverlapEvents()) { SuppressedSensors.Add(C); C->SetGenerateOverlapEvents(false); }
	}
	const auto* OtherFault=Door->FindComponentByClass<UMD_DoorFaultComponent>();
	const bool bClosed=State.bDoorLocked || bPreviousLocked || (OtherFault && OtherFault->IsFaultActive());
	if (HasAuthority() && Locked) Locked->SetPropertyValue_InContainer(Door,bClosed);
	if (Open->GetPropertyValue_InContainer(Door)!=!bClosed)
	{
		Open->SetPropertyValue_InContainer(Door,!bClosed); Door->ProcessEvent(Apply,nullptr);
		if (HasAuthority()) { Door->FlushNetDormancy(); Door->ForceNetUpdate(); }
	}
}

void AMD_RepairInstructionController::ReleaseDoor()
{
	if (!bDoorOwned) return;
	bDoorOwned=false; AActor* Door=ManagedDoor.Get();
	if (IsValid(Door) && !Door->IsActorBeingDestroyed())
	{
		const auto* Other=Door->FindComponentByClass<UMD_DoorFaultComponent>();
		const bool bLocked=bPreviousLocked || (Other && Other->IsFaultActive());
		if (HasAuthority())
		{
			if (auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"))) P->SetPropertyValue_InContainer(Door,bLocked);
			if (auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen")))
				if (UFunction* Apply=Door->FindFunction(TEXT("OnRep_bDoorOpen")); Apply && Apply->NumParms==0)
				{ P->SetPropertyValue_InContainer(Door,!bLocked); Door->ProcessEvent(Apply,nullptr); }
			Door->FlushNetDormancy(); Door->ForceNetUpdate();
		}
	}
	auto Sensors=MoveTemp(SuppressedSensors);
	for (const auto& C:Sensors) if (C.IsValid()) C->SetGenerateOverlapEvents(true);
	ManagedDoor.Reset();
}

void AMD_RepairInstructionController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!State.bActive) return;
	if (HasAuthority())
	{
		if (!HasValidConfiguration()) { Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId); return; }
		bool bAliveReader=false;
		for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
		{
			const APawn* Pawn=It->IsValid()?It->Get()->GetPawn():nullptr;
			const auto* Interaction=IsValid(Pawn)?Pawn->FindComponentByClass<UMD_InteractionComponent>():nullptr;
			if(Interaction && Interaction->CanOwnerInteract() && IsInInstructionRoom(Pawn)) { bAliveReader=true; break; }
		}
		if (State.bDoorLocked && (GetParticipantCount()==1 || !bAliveReader)) { State.bDoorLocked=false; PublishState(); }
	}
	ApplyDoorState();
}

AMD_RepairInstructionControl::AMD_RepairInstructionControl()
{
	bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=0.1f;
	InteractionBounds=CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds")); SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(6,65,70)); InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore); InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace,ECR_Block);
	Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body")); Body->SetupAttachment(RootComponent); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); if(Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
	ScreenSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenSurface")); ScreenSurface->SetupAttachment(RootComponent); ScreenSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if(Cube.Succeeded()) ScreenSurface->SetStaticMesh(Cube.Object); ScreenSurface->SetRelativeLocation(FVector(4,0,0)); ScreenSurface->SetRelativeScale3D(FVector(.005,1.15,1.05));
	AuxiliaryLightBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuxiliaryLightBody")); AuxiliaryLightBody->SetupAttachment(RootComponent); AuxiliaryLightBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Heading=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Heading")); Heading->SetupAttachment(RootComponent);
	Heading->SetHorizontalAlignment(EHTA_Center); Heading->SetWorldSize(8); Heading->SetRelativeLocation(FVector(7,0,72));
	AuxiliaryLight=CreateDefaultSubobject<USpotLightComponent>(TEXT("AuxiliaryLight")); AuxiliaryLight->SetupAttachment(RootComponent);
	AuxiliaryLight->SetRelativeLocation(FVector(45,0,110)); AuxiliaryLight->SetRelativeRotation(FRotator(-70,180,0));
	AuxiliaryLight->SetIntensity(600); AuxiliaryLight->SetAttenuationRadius(240); AuxiliaryLight->SetOuterConeAngle(38); AuxiliaryLight->SetCastShadows(false);
	for(int32 I=0;I<28;++I)
	{
		auto* M=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Marker_%02d"),I)); M->SetupAttachment(RootComponent);
		if(Cube.Succeeded()) M->SetStaticMesh(Cube.Object); M->SetCollisionEnabled(ECollisionEnabled::NoCollision); Markers.Add(M);
	}
	for(int32 I=0;I<9;++I)
	{
		auto* T=CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("Label_%02d"),I)); T->SetupAttachment(RootComponent);
		T->SetHorizontalAlignment(EHTA_Center); T->SetWorldSize(7); Labels.Add(T);
	}
}

void AMD_RepairInstructionControl::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMD_RepairInstructionControl,Controller);
	DOREPLIFETIME(AMD_RepairInstructionControl,ControlKind); DOREPLIFETIME(AMD_RepairInstructionControl,Column);
}

void AMD_RepairInstructionControl::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); RefreshPresentation(); }

namespace
{
void FitInstructionMesh(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size)
{
	const UStaticMesh* Mesh=Component->GetStaticMesh(); if(!Mesh) return;
	const auto Bounds=Mesh->GetBounds(); const FVector NativeSize=Bounds.BoxExtent*2.;
	if(NativeSize.GetMin()<=SMALL_NUMBER) return;
	const FQuat Rotation=Component->GetRelativeRotation().Quaternion();
	const FVector Scale=Rotation.UnrotateVector(Size).GetAbs()/NativeSize;
	Component->SetRelativeScale3D(Scale); Component->SetRelativeLocation(Center-Rotation.RotateVector(Bounds.Origin*Scale));
}
}

void AMD_RepairInstructionControl::RefreshPresentation()
{
	const bool bGuide=ControlKind==EMD_InstructionControlKind::Guide;
	const bool bKnob=ControlKind==EMD_InstructionControlKind::Knob;
	const bool bActive=IsValid(Controller) && Controller->State.bActive;
	const auto* State=IsValid(Controller)?&Controller->State:nullptr;
	FitInstructionMesh(Body,FVector::ZeroVector,bGuide?FVector(4,125,130):bKnob?FVector(5,24,120):FVector(5,50,25));
	ScreenSurface->SetVisibility(bGuide); ScreenSurface->SetRelativeScale3D(FVector(.005,1.15,1.05)); AuxiliaryLightBody->SetVisibility(bGuide);
	InteractionBounds->SetBoxExtent(bGuide?FVector(7,65,70):bKnob?FVector(7,14,66):FVector(7,28,17));
	AuxiliaryLight->SetVisibility(bGuide && bActive);
	Heading->SetRelativeLocation(FVector(8,0,bGuide?72:bKnob?70:0));
	Heading->SetText(FText::FromString(bGuide?TEXT("REPAIR INSTRUCTIONS"):bKnob?FString::Printf(TEXT("COL %d"),Column+1):TEXT("RESTART\nOPEN DOOR")));
	for(int32 I=0;I<28;++I) Markers[I]->SetVisibility(bGuide || (bKnob && (I==20 || I==24)));
	for(int32 I=0;I<9;++I) Labels[I]->SetVisibility(bGuide || (bKnob && I>=4));
	if(bGuide)
	{
		for(int32 C=0;C<4;++C)
		{
			const double Y=45-C*30;
			Labels[C]->SetText(FText::AsNumber(C+1)); Labels[C]->SetRelativeLocation(FVector(8,Y,58));
			Markers[20+C]->SetRelativeLocation(FVector(5,Y,0)); Markers[20+C]->SetRelativeScale3D(FVector(.01,.015,.85));
			for(int32 R=0;R<5;++R)
			{ Markers[C*5+R]->SetRelativeLocation(FVector(6,Y,40-R*20)); Markers[C*5+R]->SetRelativeScale3D(FVector(.015,.08,.015)); }
			const int32 R=State && State->TargetRows.IsValidIndex(C)?State->TargetRows[C]:0;
			Markers[24+C]->SetVisibility(bActive); FitInstructionMesh(Markers[24+C],FVector(8,Y,40-R*20),FVector(3.5,20,5.5));
		}
	}
	else if(bKnob)
	{
		Markers[20]->SetRelativeLocation(FVector(5,0,0)); Markers[20]->SetRelativeScale3D(FVector(.01,.02,.85));
		const int32 R=State && State->CurrentRows.IsValidIndex(Column)?State->CurrentRows[Column]:0;
		FitInstructionMesh(Markers[24],FVector(8,0,40-R*20),FVector(4,20,5.5));
	}
	for(int32 R=0;R<5;++R)
	{ Labels[4+R]->SetText(FText::AsNumber(R+1)); Labels[4+R]->SetRelativeLocation(FVector(8,bGuide?59:9,40-R*20)); }
	PresentedRevision=State?State->Revision:0; PresentedRow=State && State->CurrentRows.IsValidIndex(Column)?State->CurrentRows[Column]:0; bPresentedActive=bActive;
}

void AMD_RepairInstructionControl::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if(IsValid(Controller) && (PresentedRevision!=Controller->State.Revision || bPresentedActive!=Controller->State.bActive
		|| (ControlKind==EMD_InstructionControlKind::Knob && Controller->State.CurrentRows.IsValidIndex(Column) && PresentedRow!=Controller->State.CurrentRows[Column]))) RefreshPresentation();
}

bool AMD_RepairInstructionControl::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(Controller) && Controller->State.bActive && IsValid(Interactor) && Interactor->IsA<APawn>()
		&& (ControlKind!=EMD_InstructionControlKind::Knob || Controller->State.bGuideRead);
}

void AMD_RepairInstructionControl::Interact_Implementation(AActor* Interactor)
{
	const APawn* Pawn=Cast<APawn>(Interactor);
	// Never route a remote server's generic Interact back through a fresh revision.
	if(!IsValid(Controller) || !Pawn || !Pawn->IsLocallyControlled()) return;
	if(auto* Interaction=IsValid(Interactor)?Interactor->FindComponentByClass<UMD_InteractionComponent>():nullptr)
		Interaction->RequestInstructionRepair(this,Controller->State.Revision);
}

void AMD_RepairInstructionControl::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	Body->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue); Body->SetRenderCustomDepth(bHighlighted);
}

FText AMD_RepairInstructionControl::GetInteractionDisplayName_Implementation() const
{
	if(ControlKind==EMD_InstructionControlKind::Guide) return NSLOCTEXT("MDInstruction","Guide","수리 지침");
	if(ControlKind==EMD_InstructionControlKind::Restart) return NSLOCTEXT("MDInstruction","Restart","지침 재시작");
	return FText::Format(NSLOCTEXT("MDInstruction","Knob","차단기 {0}열"),FText::AsNumber(Column+1));
}

FText AMD_RepairInstructionControl::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 수리 사건이 없는 장치의 정상 상태 안내 유지
	if (IsValid(Controller) == false || Controller->State.bActive == false)
	{
		// 비활성 상태에서는 조작 행동 대신 전력 상태 표시
		return NSLOCTEXT("MDInstruction", "Idle", "전력 정상");
	}
	// 재시작 장치에 지정된 행동 유형 선택
	if (ControlKind == EMD_InstructionControlKind::Restart)
	{
		// 현재 입력 키와 재시작 행동의 설정 문구 반환
		return UMD_InteractionPresentationLibrary::FindActionPromptByType(RestartPromptType, Interactor);
	}
	// 지침 장치에 지정된 행동 유형 선택
	if (ControlKind == EMD_InstructionControlKind::Guide)
	{
		// 현재 입력 키와 지침 행동의 설정 문구 반환
		return UMD_InteractionPresentationLibrary::FindActionPromptByType(GuidePromptType, Interactor);
	}
	// 노브 행동의 설정 문구를 빈 안내 검사와 현재 행 표시에서 공유
	FText ActionPrompt = UMD_InteractionPresentationLibrary::FindActionPromptByType(KnobPromptType, Interactor);
	// 표시 안 함 유형이나 비어 있는 설정 문구 존중
	if (ActionPrompt.IsEmpty() == true)
	{
		// 행동 문구가 없는 경우 부가 행 정보도 생략
		return FText::GetEmpty();
	}
	// 기존 노브 행 위치를 선택한 행동 문구 뒤에 표시
	return FText::Format(NSLOCTEXT("MDInstruction", "TypedKnobPrompt", "{0} · 현재 위에서 {1}칸"), ActionPrompt,
		FText::AsNumber(Controller->State.CurrentRows.IsValidIndex(Column) ? Controller->State.CurrentRows[Column] + 1 : 1));
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GMDStartInstruction(TEXT("MD.BreakInstruction"),TEXT("Start the configured repair-instruction incident on the host/server; does not schedule or create a wire blackout."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if(World && World->IsGameWorld() && World->GetNetMode()!=NM_Client) for(TActorIterator<AMD_RepairInstructionController> It(World);It;++It) if(It->StartRepairIncident()) break; }),ECVF_Cheat);
#endif
