#include "Component/Interaction/MD_GlassDrawingFocusComponent.h"

#include "AbilitySystemComponent.h"
#include "CommonActivatableWidget.h"
#include "GameplayEffect.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/MD_Character.h"
#include "Component/Comfort/MD_ComfortComponent.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Component/Equipment/MD_GlassMarkerComponent.h"
#include "Component/Equipment/MD_ThrowComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Subsystem/World/MD_GlassDrawingSubsystem.h"
#include "TimerManager.h"
#include "UI/MD_LocalUIManager.h"
#include "UI/Foundation/MD_WidgetStack.h"
#include "World/Interaction/MD_GlassDrawingDesk.h"
#include "World/Wall/MD_GlassDrawingWall.h"

namespace
{
	constexpr float WatchdogIntervalSeconds = 0.2f;
	constexpr float ExitAcknowledgementAllowanceSeconds = 0.5f;
	constexpr float StationaryToleranceCm = 5.0f;
	constexpr float CameraArrivalToleranceCm = 0.5f;
	constexpr float CameraArrivalToleranceDegrees = 0.1f;
	constexpr float BodyRevealDistanceCm = 40.0f;
	constexpr float EntryHandshakeAllowanceSeconds = 5.0f;

	bool IsValidPointer(const FVector2D& Pointer)
	{
		return !Pointer.ContainsNaN() && Pointer.X >= 0.0 && Pointer.X <= 1.0
			&& Pointer.Y >= 0.0 && Pointer.Y <= 1.0;
	}
}

UMD_GlassDrawingFocusComponent::UMD_GlassDrawingFocusComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UMD_GlassDrawingFocusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMD_GlassDrawingFocusComponent, ActiveDesk);
	DOREPLIFETIME_CONDITION(UMD_GlassDrawingFocusComponent, SessionSerial, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMD_GlassDrawingFocusComponent, ReadySerial, COND_OwnerOnly);
}

AMD_Character* UMD_GlassDrawingFocusComponent::GetCharacter() const
{
	return Cast<AMD_Character>(GetOwner());
}

bool UMD_GlassDrawingFocusComponent::IsFocusActive() const
{
	return IsValid(ActiveDesk);
}

bool UMD_GlassDrawingFocusComponent::IsDead() const
{
	const AMD_Character* Character = GetCharacter();
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	return !IsValid(ASC) || ASC->HasMatchingGameplayTag(MDGameplayTags::State_Dead);
}

bool UMD_GlassDrawingFocusComponent::CanBeginFocus(const AMD_GlassDrawingDesk* Desk) const
{
	const AMD_Character* Character = GetCharacter();
	const APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	const UMD_InteractionComponent* Interaction = Character ? Character->GetInteractionComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!IsValid(Desk) || !Desk->HasValidConfiguration() || IsFocusActive() || IsDead()
		|| !IsValid(Character) || Character->IsActorBeingDestroyed() || !IsValid(PC)
		|| PC->GetPawn() != Character || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()
		|| !IsValid(Interaction) || !Interaction->CanOwnerInteract() || Interaction->IsHolding()
		|| !GetWorld() || GetWorld()->IsPaused() || !IsValid(Movement)) return false;
	// Custom movement owns additional state (for example its ladder); do not discard that state.
	if (Movement->MovementMode != MOVE_Walking && Movement->MovementMode != MOVE_NavWalking
		&& Movement->MovementMode != MOVE_Swimming) return false;
	return PC->GetViewTarget() == Character;
}

bool UMD_GlassDrawingFocusComponent::BeginFocus(AMD_GlassDrawingDesk* Desk)
{
	AMD_Character* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !CanBeginFocus(Desk) || Desk->GetOccupant() != Character) return false;
	UMD_EquipHotbarComponent* Hotbar = Character->GetHotbarComponent();
	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	UMD_ToolDefinition* Tool = Desk->GetMarkerTool();
	if (!Hotbar || !ASC || !Tool || !Tool->AbilityGrantEffect) return false;
	FVector Location;
	FRotator Rotation;
	float FOV;
	float Reach;
	if (!Desk->ResolveFocusView(Character, FocusAspect, Location, Rotation, FOV, Reach)) return false;

	if (!Hotbar->FindItemAbility(Tool).IsValid())
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Tool);
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(Tool->AbilityGrantEffect, 1.0f, Context);
		if (!Spec.IsValid()) return false;
		TemporaryGrant = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		if (!TemporaryGrant.IsValid() || !Hotbar->FindItemAbility(Tool).IsValid())
		{
			if (TemporaryGrant.IsValid()) ASC->RemoveActiveGameplayEffect(TemporaryGrant);
			TemporaryGrant.Invalidate();
			return false;
		}
		GrantedASC = ASC;
	}

	Character->CancelScrapeInteraction();
	if (Character->GetThrowComponent()) Character->GetThrowComponent()->CancelThrow();
	ActiveDesk = Desk;
	if (++SessionSerial == 0) ++SessionSerial;
	ReadySerial = 0;
	bServerExiting = false;
	bViewConfigured = false;
	ServerViewStartedAt = 0.0;
	SessionController = Cast<APlayerController>(Character->GetController());
	OriginalPawnLocation = Character->GetActorLocation();
	SessionGlass = Desk->GetTargetGlassMesh();
	SessionGlassActor = SessionGlass->GetOwner();
	SessionTool = Tool;
	OriginalGlassTransform = SessionGlass->GetComponentTransform();
	ServerLeaseStartedAt = GetWorld()->GetTimeSeconds();
	FocusTraceDistance = 0.0f;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	OriginalMovementMode = Movement->MovementMode;
	OriginalCustomMovementMode = Movement->CustomMovementMode;
	Movement->StopMovementImmediately();
	Movement->DisableMovement();
	SessionGlassActor->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleGlassEndPlay);
	GetWorld()->GetTimerManager().SetTimer(WatchdogTimer, this, &ThisClass::CheckSession, WatchdogIntervalSeconds, true);
	RefreshToolPresentation();
	Character->ForceNetUpdate();
	ClientBeginFocus(Desk, SessionSerial);
	return true;
}

UMD_ToolDefinition* UMD_GlassDrawingFocusComponent::GetMarkerTool() const
{
	return IsValid(ActiveDesk) ? ActiveDesk->GetMarkerTool() : nullptr;
}

UStaticMeshComponent* UMD_GlassDrawingFocusComponent::GetTargetGlassMesh() const
{
	return IsValid(ActiveDesk) ? ActiveDesk->GetTargetGlassMesh() : nullptr;
}

bool UMD_GlassDrawingFocusComponent::IsSessionValid() const
{
	const AMD_Character* Character = GetCharacter();
	const UMD_InteractionComponent* Interaction = Character ? Character->GetInteractionComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	UStaticMeshComponent* Glass = GetTargetGlassMesh();
	return IsValid(Character) && !Character->IsActorBeingDestroyed() && !IsDead()
		&& IsValid(ActiveDesk) && ActiveDesk->GetOccupant() == Character && ActiveDesk->HasValidConfiguration()
		&& IsValid(SessionController.Get()) && SessionController->GetPawn() == Character
		&& Character->GetController() == SessionController.Get()
		&& !SessionController->IsMoveInputIgnored() && !SessionController->IsLookInputIgnored()
		&& IsValid(Interaction) && Interaction->CanOwnerInteract() && !Interaction->IsHolding()
		&& IsValid(Movement) && Movement->MovementMode == MOVE_None
		&& FVector::DistSquared(Character->GetActorLocation(), OriginalPawnLocation) <= FMath::Square(StationaryToleranceCm)
		&& IsValid(Glass) && Glass == SessionGlass.Get() && Glass->GetOwner() == SessionGlassActor.Get()
		&& ActiveDesk->GetMarkerTool() == SessionTool.Get()
		&& Glass->GetComponentTransform().Equals(OriginalGlassTransform, 0.01f);
}

bool UMD_GlassDrawingFocusComponent::IsDrawingReady() const
{
	const AMD_Character* Character = GetCharacter();
	if (!Character || !IsFocusActive() || bServerExiting || ReadySerial == 0
		|| ReadySerial != (Character->HasAuthority() ? SessionSerial : LocalSerial)) return false;
	return Character->HasAuthority() ? IsSessionValid()
		: Character->IsLocallyControlled() && LocalPhase == EMD_GlassDrawingFocusPhase::Drawing;
}

void UMD_GlassDrawingFocusComponent::ClientBeginFocus_Implementation(AMD_GlassDrawingDesk* Desk, uint32 Serial)
{
	AMD_Character* Character = GetCharacter();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !Character->IsLocallyControlled() || !PC || !IsValid(Desk) || Serial == 0) return;
	if (LocalPhase != EMD_GlassDrawingFocusPhase::Idle) FinishLocalExit();
	Character->CancelScrapeInteraction();
	if (Character->GetThrowComponent()) Character->GetThrowComponent()->CancelThrow();
	ActiveDesk = Desk;
	SessionSerial = Serial;
	LocalSerial = Serial;
	SessionController = PC;
	OriginalViewTarget = PC->GetViewTarget();
	OriginalControlRotation = PC->GetControlRotation();
	bOriginalThirdPerson = Character->IsLocalThirdPersonViewEnabled();
	PointerNormalized = FVector2D(0.5, 0.5);
	LocalPhase = EMD_GlassDrawingFocusPhase::Entering;
	LocalTransitionStartedAt = GetWorld()->GetTimeSeconds();
	if (!Character->HasAuthority())
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	LastReadyRequestAt = -1.0;
	LocalBlendSeconds = Desk->BlendSeconds;
	if (UMD_ComfortComponent* Comfort = Character->FindComponentByClass<UMD_ComfortComponent>()) Comfort->ResetPendingLookInput();
	int32 Width = 0;
	int32 Height = 0;
	PC->GetViewportSize(Width, Height);
	const float Aspect = Width > 0 && Height > 0 ? float(Width) / Height : 16.0f / 9.0f;
	ServerConfigureView(Serial, Aspect);
	SetComponentTickEnabled(true);
}

void UMD_GlassDrawingFocusComponent::ServerConfigureView_Implementation(uint32 Serial, float Aspect)
{
	if (Serial != SessionSerial || bServerExiting || !IsSessionValid() || ReadySerial != 0 || bViewConfigured
		|| !FMath::IsFinite(Aspect) || Aspect < 0.5f || Aspect > 4.0f) return;
	FVector Location;
	FRotator Rotation;
	float FOV;
	float Reach;
	if (!ActiveDesk->ResolveFocusView(GetCharacter(), Aspect, Location, Rotation, FOV, Reach))
	{
		EndFocus(true);
		return;
	}
	FocusViewLocation = Location;
	FocusViewRotation = Rotation;
	FocusFOV = FOV;
	FocusAspect = Aspect;
	FocusTraceDistance = Reach;
	bViewConfigured = true;
	ServerViewStartedAt = GetWorld()->GetTimeSeconds();
	ClientConfigureView(Serial, Location, Rotation, FOV, Aspect);
}

void UMD_GlassDrawingFocusComponent::ClientConfigureView_Implementation(uint32 Serial, FVector Location,
	FRotator Rotation, float FieldOfView, float Aspect)
{
	if (Serial != LocalSerial || LocalPhase != EMD_GlassDrawingFocusPhase::Entering || !IsValid(ActiveDesk)) return;
	AMD_Character* Character = GetCharacter();
	APlayerController* PC = SessionController.Get();
	if (!Character || !PC) return;
	FocusViewLocation = Location;
	FocusViewRotation = Rotation;
	FocusFOV = FieldOfView;
	FocusAspect = Aspect;
	FActorSpawnParameters Spawn;
	Spawn.Owner = Character;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FocusCamera = GetWorld()->SpawnActor<ACameraActor>(Location, Rotation, Spawn);
	if (!IsValid(FocusCamera))
	{
		EndFocus(true);
		return;
	}
	UCameraComponent* Camera = FocusCamera->GetCameraComponent();
	Camera->SetFieldOfView(FieldOfView);
	// Use the full viewport throughout both blends; the pane fit already uses its aspect.
	Camera->AspectRatio = Aspect;
	Camera->bConstrainAspectRatio = false;
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
	LocalTransitionStartedAt = GetWorld()->GetTimeSeconds();
	bChangingView = true;
	PC->SetViewTargetWithBlend(FocusCamera, LocalBlendSeconds, VTBlend_Cubic, 0.0f, true);
	bChangingView = false;
	// 카메라 구성이 완료된 소유 클라이언트의 태그 화면 표시
	OpenDrawingWidget();
}

void UMD_GlassDrawingFocusComponent::OpenDrawingWidget()
{
	// 전용 벽의 설정만 사용하여 기존 책상 상호작용 보존
	AMD_GlassDrawingWall* Wall = Cast<AMD_GlassDrawingWall>(ActiveDesk);
	// 태그를 지정하지 않은 벽의 기존 그리기 동작 유지
	if (IsValid(Wall) == false || Wall->GetDrawingWidgetTag().IsValid() == false)
	{
		// 화면 표시가 필요 없는 세션의 UI 처리 종료
		return;
	}
	// 현재 세션을 소유한 로컬 플레이어 조회
	APlayerController* PC = SessionController.Get();
	// 소유자 이탈 후 화면 생성 차단
	if (IsValid(PC) == false || IsValid(PC->GetLocalPlayer()) == false)
	{
		// 화면을 표시할 수 없는 상호작용의 점유 해제
		EndFocus(true);
		// 잘못된 로컬 플레이어의 UI 접근 차단
		return;
	}
	// 플레이어별 태그 및 레이어 등록 관리자 조회
	UMD_LocalUIManager* UIManager = PC->GetLocalPlayer()->GetSubsystem<UMD_LocalUIManager>();
	// 초기화되지 않은 UI 관리자 접근 차단
	if (IsValid(UIManager) == false)
	{
		// 화면 생성 실패에 따른 이동과 점유 상태 복원
		EndFocus(true);
		// 사용할 수 없는 UI 관리자 처리 종료
		return;
	}
	// 전환 중 종료에도 화면을 제거할 실제 등록 레이어 조회
	FGameplayTag LayerTag;
	// 기존 UI 등록 조회에 필요한 화면 클래스 출력값
	TSoftClassPtr<UCommonActivatableWidget> WidgetClass;
	// 화면 활성화 콜백에서 세션이 종료되어도 유지할 스택 참조
	UMD_WidgetStack* Stack = nullptr;
	// 태그에 해당하는 스택이 등록된 경우 소유 스택 보관
	if (UIManager->ResolveWidgetRegistration(Wall->GetDrawingWidgetTag(), LayerTag, WidgetClass) == true)
	{
		// 활성화 대기 중인 화면도 제거 가능한 스택 참조 저장
		Stack = UIManager->FindLayerStack(LayerTag);
	}
	// 프로젝트 설정에 등록된 태그의 화면을 소유 플레이어에게 표시
	DrawingWidget = UIManager->PushScreenToLayerByTag(Wall->GetDrawingWidgetTag());
	// 화면 생성 중 재진입 종료 이후에도 제거 가능한 스택 보관
	DrawingWidgetStack = Stack;
	// 미등록 태그나 클래스 로드 실패의 세션 잔류 방지
	if (IsValid(DrawingWidget.Get()) == false)
	{
		// 실패한 UI 표시 요청의 상호작용 종료
		EndFocus(true);
		// 생성되지 않은 화면의 구독 차단
		return;
	}
	// 화면의 활성화 동작에서 이미 종료된 세션의 UI 잔류 방지
	if (LocalPhase != EMD_GlassDrawingFocusPhase::Entering)
	{
		// 생성 도중 취소된 상호작용의 화면 제거
		CloseDrawingWidget();
		// 종료된 세션의 닫힘 구독 차단
		return;
	}
	// 외부 Pop 또는 Deactivate 시에도 유리 점유 해제 연결
	DrawingWidgetClosedHandle = DrawingWidget->OnDeactivated().AddUObject(this, &ThisClass::HandleDrawingWidgetClosed);
}

void UMD_GlassDrawingFocusComponent::CloseDrawingWidget()
{
	// 정리 중 콜백과 분리할 기존 화면 참조 보관
	UCommonActivatableWidget* Widget = DrawingWidget.Get();
	// 화면 제거 중 참조 정리와 분리할 소유 스택 보관
	UMD_WidgetStack* Stack = DrawingWidgetStack.Get();
	// 화면 비활성화 재진입에 앞선 세션 참조 해제
	DrawingWidget.Reset();
	// 종료된 세션의 스택 참조 해제
	DrawingWidgetStack.Reset();
	// 살아 있는 세션 화면에 한정한 구독 및 표시 정리
	if (IsValid(Widget) == true)
	{
		// 상호작용 종료에 의한 Deactivate의 중복 종료 요청 차단
		Widget->OnDeactivated().Remove(DrawingWidgetClosedHandle);
		// 활성화 전 전환 중인 화면까지 소유 스택에서 제거
		if (IsValid(Stack) == true)
		{
			// 대기 화면 취소 시 이전 화면 활성화까지 복원하는 제거
			Stack->RemoveWidgetAndRestore(Widget);
		}
		// 스택이 먼저 제거된 경우 남은 화면의 직접 비활성화
		else
		{
			// 스택 수명과 무관한 세션 화면의 입력 및 표시 종료
			Widget->DeactivateWidget();
		}
	}
	// 종료된 화면의 구독 식별자 초기화
	DrawingWidgetClosedHandle.Reset();
}

void UMD_GlassDrawingFocusComponent::HandleDrawingWidgetClosed()
{
	// 외부에서 닫힌 화면의 구독과 참조 정리
	CloseDrawingWidget();
	// 원래 시점으로 복귀하면서 서버의 유리 점유 해제 요청
	EndFocus(false);
}

void UMD_GlassDrawingFocusComponent::ServerReady_Implementation(uint32 Serial)
{
	if (Serial != SessionSerial || bServerExiting || !bViewConfigured || !IsSessionValid() || FocusTraceDistance <= 0.0f) return;
	if (!GetCharacter()->GetHotbarComponent()->FindItemAbility(GetMarkerTool()).IsValid()) return;
	if (GetWorld()->GetTimeSeconds() - ServerViewStartedAt + UE_KINDA_SMALL_NUMBER < ActiveDesk->BlendSeconds) return;
	ReadySerial = Serial;
	GetOwner()->ForceNetUpdate();
}

bool UMD_GlassDrawingFocusComponent::ConsumeLookInput(const FVector2D& Delta)
{
	if (!IsFocusActive() && LocalPhase == EMD_GlassDrawingFocusPhase::Idle) return false;
	if (LocalPhase == EMD_GlassDrawingFocusPhase::Drawing && IsValid(ActiveDesk) && !Delta.ContainsNaN())
	{
		const APlayerController* PC = SessionController.Get();
		if (!IsValid(PC)) return true;
		double YawScale = 1.0;
		double PitchScale = 1.0;
		if (GetDefault<UInputSettings>()->bEnableLegacyInputScales)
		{
			// Match the scales still used by AddControllerYaw/PitchInput in this project.
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			YawScale = FMath::Abs(PC->GetDeprecatedInputYawScale());
			PitchScale = FMath::Abs(PC->GetDeprecatedInputPitchScale());
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		const double TanH = FMath::Tan(FMath::DegreesToRadians(double(FocusFOV) * 0.5));
		// Keep serialized station tuning as a relative gain; 0.002 was the original default.
		const double Gain = double(ActiveDesk->CursorSpeed) / 0.002;
		if (!FMath::IsFinite(TanH) || TanH <= UE_SMALL_NUMBER
			|| !FMath::IsFinite(FocusAspect) || FocusAspect <= UE_SMALL_NUMBER
			|| !FMath::IsFinite(Gain) || Gain <= 0.0) return true;
		// IA_Look already negates Y. Projection gives equal pixel speed on both axes,
		// using camera sensitivity at the center while keeping speed uniform across the pane.
		const FVector2D Change(
			FMath::DegreesToRadians(Delta.X * YawScale) * Gain / (2.0 * TanH),
			FMath::DegreesToRadians(Delta.Y * PitchScale) * Gain * FocusAspect / (2.0 * TanH));
		if (Change.ContainsNaN()) return true;
		PointerNormalized.X = FMath::Clamp(PointerNormalized.X + Change.X, 0.0, 1.0);
		PointerNormalized.Y = FMath::Clamp(PointerNormalized.Y + Change.Y, 0.0, 1.0);
	}
	return true;
}

FVector UMD_GlassDrawingFocusComponent::DirectionForPointer(const FVector2D& Pointer) const
{
	const FRotationMatrix Basis(FocusViewRotation);
	const double TanH = FMath::Tan(FMath::DegreesToRadians(double(FocusFOV) * 0.5));
	return (Basis.GetUnitAxis(EAxis::X)
		+ Basis.GetUnitAxis(EAxis::Y) * ((2.0 * Pointer.X - 1.0) * TanH)
		+ Basis.GetUnitAxis(EAxis::Z) * ((1.0 - 2.0 * Pointer.Y) * TanH / FocusAspect)).GetSafeNormal();
}

bool UMD_GlassDrawingFocusComponent::GetValidatedDrawRay(const FVector2D& Pointer,
	FVector& OutOrigin, FVector& OutDirection, float& OutReach) const
{
	const AMD_Character* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !IsDrawingReady() || !IsValidPointer(Pointer)) return false;
	OutOrigin = FocusViewLocation;
	OutDirection = DirectionForPointer(Pointer);
	OutReach = FocusTraceDistance;
	return !OutOrigin.ContainsNaN() && !OutDirection.ContainsNaN() && !OutDirection.IsNearlyZero();
}

bool UMD_GlassDrawingFocusComponent::CanAuthorSurface(const UStaticMeshComponent* Mesh, const FVector2D& UV) const
{
	const AMD_Character* Character = GetCharacter();
	if (!Character || !Character->HasAuthority() || !IsDrawingReady()
		|| Mesh != GetTargetGlassMesh() || !IsValidPointer(UV)) return false;
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	const UMD_GlassDrawingProfile* Profile = Drawing ? Drawing->GetProfile() : nullptr;
	if (!IsValid(Profile) || !Profile->IsValidUV(UV)) return false;
	const FTransform& Transform = Mesh->GetComponentTransform();
	const FVector Front = Transform.TransformPosition(Profile->UVToLocalPoint(UV, Profile->FrontFaceY));
	const FVector Back = Transform.TransformPosition(Profile->UVToLocalPoint(UV, Profile->BackFaceY));
	return FMath::Min(FVector::DistSquared(FocusViewLocation, Front), FVector::DistSquared(FocusViewLocation, Back))
		<= FMath::Square(double(FocusTraceDistance));
}

void UMD_GlassDrawingFocusComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMD_Character* Character = GetCharacter();
	APlayerController* PC = SessionController.Get();
	if (!Character || !Character->IsLocallyControlled() || !PC || PC->GetPawn() != Character)
	{
		EndFocus(true);
		return;
	}
	if (LocalPhase == EMD_GlassDrawingFocusPhase::Entering
		&& GetWorld()->GetTimeSeconds() - LocalTransitionStartedAt > EntryHandshakeAllowanceSeconds + LocalBlendSeconds)
	{
		EndFocus(true);
		return;
	}
	if (IsDead() || !IsValid(ActiveDesk) || !IsValid(FocusCamera))
	{
		// The reliable configuration RPC can arrive after the begin RPC.
		if (LocalPhase == EMD_GlassDrawingFocusPhase::Entering && IsValid(ActiveDesk)
			&& !IsDead() && !IsValid(FocusCamera)) return;
		EndFocus(true);
		return;
	}
	APlayerCameraManager* Manager = PC->PlayerCameraManager;
	if (!Manager) return;
	const AActor* ExpectedView = LocalPhase == EMD_GlassDrawingFocusPhase::Exiting
		? OriginalViewTarget.Get() : FocusCamera.Get();
	if (PC->GetViewTarget() != ExpectedView)
	{
		EndFocus(true);
		return;
	}
	PC->SetControlRotation(OriginalControlRotation);
	if (LocalPhase == EMD_GlassDrawingFocusPhase::Entering)
	{
		const UCameraComponent* PawnCamera = Character->FindComponentByClass<UCameraComponent>();
		if (PawnCamera && FVector::Distance(Manager->GetCameraLocation(), PawnCamera->GetComponentLocation()) > BodyRevealDistanceCm)
			Character->SetLocalThirdPersonViewEnabled(true);
		const bool bArrived = FVector::Distance(Manager->GetCameraLocation(), FocusViewLocation) <= CameraArrivalToleranceCm
			&& Manager->GetCameraRotation().Equals(FocusViewRotation, CameraArrivalToleranceDegrees);
		if (bArrived && GetWorld()->GetTimeSeconds() - LocalTransitionStartedAt >= LocalBlendSeconds
			&& Character->GetHotbarComponent()->FindItemAbility(GetMarkerTool()).IsValid())
		{
			LocalPhase = EMD_GlassDrawingFocusPhase::Drawing;
			Character->SetLocalThirdPersonViewEnabled(true);
			// The fixed drawing view must expose the whole pane, even when the pawn is close.
			SetLocalBodyHidden(true);
			CreateCursorPen();
			ServerReady(LocalSerial);
			LastReadyRequestAt = GetWorld()->GetTimeSeconds();
		}
	}
	else if (LocalPhase == EMD_GlassDrawingFocusPhase::Drawing)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (ReadySerial != LocalSerial && Now - LastReadyRequestAt >= WatchdogIntervalSeconds)
		{
			ServerReady(LocalSerial);
			LastReadyRequestAt = Now;
		}
		UpdateCursorPen();
	}
	else if (LocalPhase == EMD_GlassDrawingFocusPhase::Exiting)
	{
		const UCameraComponent* PawnCamera = Character->FindComponentByClass<UCameraComponent>();
		if (PawnCamera && FVector::Distance(Manager->GetCameraLocation(), PawnCamera->GetComponentLocation()) <= BodyRevealDistanceCm)
			Character->SetLocalThirdPersonViewEnabled(bOriginalThirdPerson);
		const bool bArrived = FVector::Distance(Manager->GetCameraLocation(), ReturnViewLocation) <= CameraArrivalToleranceCm
			&& Manager->GetCameraRotation().Equals(ReturnViewRotation, CameraArrivalToleranceDegrees);
		if (bArrived && GetWorld()->GetTimeSeconds() - LocalTransitionStartedAt >= LocalBlendSeconds)
			FinishLocalExit();
	}
}

void UMD_GlassDrawingFocusComponent::SetLocalBodyHidden(bool bHideBody)
{
	APlayerController* PC = SessionController.Get();
	AMD_Character* Character = GetCharacter();
	if (bHideBody)
	{
		if (!PC || !Character || !Character->IsLocallyControlled()) return;
		// OwnerNoSee compares the view target itself; an external camera is a different actor.
		// Keep this filter local to the controller and preserve pre-existing hidden components.
		TInlineComponentArray<UPrimitiveComponent*> Components(Character);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsValid(Component) && !PC->HiddenPrimitiveComponents.Contains(Component))
			{
				PC->HiddenPrimitiveComponents.Add(Component);
				HiddenBodyComponents.Add(Component);
			}
		}
	}
	else
	{
		if (PC)
		{
			for (const TWeakObjectPtr<UPrimitiveComponent>& Component : HiddenBodyComponents)
				PC->HiddenPrimitiveComponents.Remove(Component);
		}
		HiddenBodyComponents.Reset();
	}
}

void UMD_GlassDrawingFocusComponent::CreateCursorPen()
{
	if (IsValid(CursorPen) || !IsValid(FocusCamera) || !GetMarkerTool() || !IsValid(GetMarkerTool()->ToolMesh)) return;
	CursorPen = NewObject<UStaticMeshComponent>(FocusCamera, TEXT("GlassDrawingCursorPen"));
	CursorPen->SetStaticMesh(GetMarkerTool()->ToolMesh);
	CursorPen->SetMobility(EComponentMobility::Movable);
	CursorPen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CursorPen->SetGenerateOverlapEvents(false);
	CursorPen->SetCanEverAffectNavigation(false);
	CursorPen->SetCastShadow(false);
	CursorPen->SetOnlyOwnerSee(true);
	CursorPen->SetupAttachment(FocusCamera->GetRootComponent());
	FocusCamera->AddInstanceComponent(CursorPen);
	CursorPen->RegisterComponent();
	const UMD_GlassMarkerComponent* Marker = GetCharacter()->GetGlassMarkerComponent();
	const int32 Slot = Marker ? CursorPen->GetMaterialIndex(Marker->GetInkIndicatorMaterialSlot()) : INDEX_NONE;
	if (Slot != INDEX_NONE) CursorInkMaterial = CursorPen->CreateDynamicMaterialInstance(Slot);
	LastCursorColor = 255;
	UpdateCursorPen();
}

void UMD_GlassDrawingFocusComponent::UpdateCursorPen()
{
	if (!IsValid(CursorPen) || !IsValid(ActiveDesk) || !IsValid(FocusCamera)) return;
	const UMD_GlassMarkerComponent* Marker = GetCharacter()->GetGlassMarkerComponent();
	const bool bErase = Marker && Marker->IsErasing();
	const FBoxSphereBounds Bounds = CursorPen->GetStaticMesh()->GetBounds();
	const int32 AxisIndex = Bounds.BoxExtent.X >= Bounds.BoxExtent.Y && Bounds.BoxExtent.X >= Bounds.BoxExtent.Z
		? 0 : Bounds.BoxExtent.Y >= Bounds.BoxExtent.Z ? 1 : 2;
	FVector Axis = FVector::ZeroVector;
	Axis[AxisIndex] = 1.0;
	FVector Contact = Bounds.Origin + Axis * Bounds.BoxExtent[AxisIndex] * (bErase ? -1.0 : 1.0);
	const FVector AuthoredContact = bErase ? ActiveDesk->CursorEraserLocalOffset : ActiveDesk->CursorTipLocalOffset;
	if (!AuthoredContact.IsNearlyZero()) Contact = AuthoredContact;
	const FVector ViewAxis(0.65, -0.55, 0.4);
	FQuat Rotation = FocusViewRotation.Quaternion() * FQuat::FindBetweenNormals(Axis, ViewAxis.GetSafeNormal())
		* ActiveDesk->CursorRotationOffset.Quaternion();
	if (bErase)
	{
		const FVector Perpendicular = FVector::CrossProduct(Axis, AxisIndex == 2 ? FVector::RightVector : FVector::UpVector).GetSafeNormal();
		Rotation = Rotation * FQuat(Perpendicular, PI);
	}
	const FVector Direction = DirectionForPointer(PointerNormalized);
	const FVector Forward = FocusViewRotation.Vector();
	const double Depth = FMath::Clamp(ActiveDesk->CursorDepthCm, 5.0f, 300.0f);
	const FVector Tip = FocusViewLocation + Direction * (Depth / FMath::Max(0.01, FVector::DotProduct(Direction, Forward)));
	const float Scale = FMath::Clamp(ActiveDesk->CursorScale, 0.1f, 2.0f);
	CursorPen->SetWorldTransform(FTransform(Rotation, Tip - Rotation.RotateVector(Contact * Scale), FVector(Scale)));
	if (Marker && CursorInkMaterial && LastCursorColor != Marker->GetInkColorIndex())
	{
		CursorInkMaterial->SetVectorParameterValue(TEXT("MarkerInkColor"), Marker->GetSelectedInkColor());
		LastCursorColor = Marker->GetInkColorIndex();
	}
}

void UMD_GlassDrawingFocusComponent::DestroyCursorPen()
{
	CursorInkMaterial = nullptr;
	if (IsValid(CursorPen))
	{
		if (AActor* Owner = CursorPen->GetOwner()) Owner->RemoveInstanceComponent(CursorPen);
		CursorPen->DestroyComponent();
	}
	CursorPen = nullptr;
}

void UMD_GlassDrawingFocusComponent::EndFocus(bool bImmediate)
{
	AMD_Character* Character = GetCharacter();
	if (Character && Character->HasAuthority())
	{
		if (!IsFocusActive()) { if (LocalPhase != EMD_GlassDrawingFocusPhase::Idle) StartLocalExit(true); return; }
		if (bServerExiting && !bImmediate) return;
		bServerExiting = true;
		ReadySerial = 0;
		ServerExitStartedAt = GetWorld()->GetTimeSeconds();
		const APlayerState* PlayerState = Character->GetPlayerState();
		const float RoundTripAllowance = PlayerState ? PlayerState->GetPingInMilliseconds() * 0.002f : 0.0f;
		const float ExitTimeout = ActiveDesk->BlendSeconds
			+ FMath::Clamp(ExitAcknowledgementAllowanceSeconds + RoundTripAllowance, 0.5f, 5.0f);
		if (Character->GetGlassMarkerComponent()) Character->GetGlassMarkerComponent()->CancelUse();
		ClientExitFocus(SessionSerial, bImmediate);
		if (bImmediate) FinalizeServerFocus();
		else if (IsFocusActive()) GetWorld()->GetTimerManager().SetTimer(ExitFallbackTimer, this, &ThisClass::FinalizeServerFocus,
			FMath::Max(0.01f, ExitTimeout), false);
	}
	else if (Character && Character->IsLocallyControlled())
	{
		if (LocalPhase == EMD_GlassDrawingFocusPhase::Idle) return;
		if (Character->GetGlassMarkerComponent()) Character->GetGlassMarkerComponent()->CancelUse();
		ServerRequestExit(LocalSerial, bImmediate);
		StartLocalExit(bImmediate);
	}
}

void UMD_GlassDrawingFocusComponent::ServerRequestExit_Implementation(uint32 Serial, bool bImmediate)
{
	if (Serial == SessionSerial && IsFocusActive()) EndFocus(bImmediate);
}

void UMD_GlassDrawingFocusComponent::ClientExitFocus_Implementation(uint32 Serial, bool bImmediate)
{
	if (Serial != LocalSerial || LocalPhase == EMD_GlassDrawingFocusPhase::Idle) return;
	StartLocalExit(bImmediate);
}

void UMD_GlassDrawingFocusComponent::StartLocalExit(bool bImmediate)
{
	// 뒤로가기와 기존 상호작용 종료 경로의 화면 정리
	CloseDrawingWidget();
	if (LocalPhase == EMD_GlassDrawingFocusPhase::Idle) return;
	if (LocalPhase == EMD_GlassDrawingFocusPhase::Exiting && !bImmediate) return;
	DestroyCursorPen();
	APlayerController* PC = SessionController.Get();
	AMD_Character* Character = GetCharacter();
	SetLocalBodyHidden(false);
	AActor* Target = OriginalViewTarget.Get();
	if (!PC || !Character || PC->GetPawn() != Character || !IsValid(Target) || Target->IsActorBeingDestroyed()
		|| Character->IsActorBeingDestroyed() || !IsValid(FocusCamera))
	{
		FinishLocalExit();
		return;
	}
	const bool bOwnsView = PC->GetViewTarget() == FocusCamera;
	if (!bOwnsView) { FinishLocalExit(); return; }
	PC->SetControlRotation(OriginalControlRotation);
	if (UMD_ComfortComponent* Comfort = Character->FindComponentByClass<UMD_ComfortComponent>()) Comfort->ResetPendingLookInput();
	const UCameraComponent* PawnCamera = Character->FindComponentByClass<UCameraComponent>();
	ReturnViewLocation = PawnCamera ? PawnCamera->GetComponentLocation() : Character->GetPawnViewLocation();
	ReturnViewRotation = OriginalControlRotation;
	// Start the reverse blend from the visible POV, including an interrupted entry blend.
	if (PC->PlayerCameraManager)
	{
		FocusCamera->SetActorLocationAndRotation(PC->PlayerCameraManager->GetCameraLocation(), PC->PlayerCameraManager->GetCameraRotation());
		FocusCamera->GetCameraComponent()->SetFieldOfView(PC->PlayerCameraManager->GetFOVAngle());
	}
	bChangingView = true;
	PC->SetViewTarget(FocusCamera);
	PC->SetViewTargetWithBlend(Target, bImmediate ? 0.0f : LocalBlendSeconds, VTBlend_Cubic, 0.0f, true);
	bChangingView = false;
	LocalPhase = EMD_GlassDrawingFocusPhase::Exiting;
	LocalTransitionStartedAt = GetWorld()->GetTimeSeconds();
	if (bImmediate) FinishLocalExit();
}

void UMD_GlassDrawingFocusComponent::FinishLocalExit()
{
	// 세션 교체와 강제 종료에서도 이전 화면 제거
	CloseDrawingWidget();
	const uint32 FinishedSerial = LocalSerial;
	AMD_Character* Character = GetCharacter();
	SetLocalBodyHidden(false);
	if (Character && Character->IsLocallyControlled()) Character->SetLocalThirdPersonViewEnabled(bOriginalThirdPerson);
	DestroyCursorPen();
	if (IsValid(FocusCamera)) FocusCamera->Destroy();
	FocusCamera = nullptr;
	LocalPhase = EMD_GlassDrawingFocusPhase::Idle;
	LocalSerial = 0;
	LastReadyRequestAt = -1.0;
	SetComponentTickEnabled(false);
	if (FinishedSerial != 0 && Character && Character->IsLocallyControlled()) ServerFinishedExit(FinishedSerial);
}

void UMD_GlassDrawingFocusComponent::ServerFinishedExit_Implementation(uint32 Serial)
{
	if (Serial == SessionSerial && bServerExiting && IsValid(ActiveDesk)
		&& GetWorld()->GetTimeSeconds() - ServerExitStartedAt + UE_KINDA_SMALL_NUMBER >= ActiveDesk->BlendSeconds)
		FinalizeServerFocus();
}

void UMD_GlassDrawingFocusComponent::FinalizeServerFocus()
{
	AMD_Character* Character = GetCharacter();
	if (!Character || !Character->HasAuthority()) return;
	AMD_GlassDrawingDesk* Desk = ActiveDesk.Get();
	if (SessionGlassActor.IsValid()) SessionGlassActor->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleGlassEndPlay);
	SessionGlass.Reset();
	SessionGlassActor.Reset();
	SessionTool.Reset();
	GetWorld()->GetTimerManager().ClearTimer(WatchdogTimer);
	GetWorld()->GetTimerManager().ClearTimer(ExitFallbackTimer);
	if (Character->GetGlassMarkerComponent()) Character->GetGlassMarkerComponent()->CancelUse();
	if (TemporaryGrant.IsValid() && GrantedASC.IsValid()) GrantedASC->RemoveActiveGameplayEffect(TemporaryGrant);
	TemporaryGrant.Invalidate();
	GrantedASC.Reset();
	ActiveDesk = nullptr;
	ReadySerial = 0;
	bServerExiting = false;
	bViewConfigured = false;
	if (IsValid(Desk)) Desk->ReleaseOccupant(Character);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Character->IsActorBeingDestroyed() && !IsDead() && SessionController.IsValid()
		&& SessionController->GetPawn() == Character && Movement && Movement->MovementMode == MOVE_None)
		Movement->SetMovementMode(OriginalMovementMode, OriginalCustomMovementMode);
	RefreshToolPresentation();
	Character->ForceNetUpdate();
}

void UMD_GlassDrawingFocusComponent::CheckSession()
{
	if (!IsSessionValid() || (!bServerExiting && ReadySerial == 0
		&& GetWorld()->GetTimeSeconds() - ServerLeaseStartedAt > EntryHandshakeAllowanceSeconds + ActiveDesk->BlendSeconds))
		EndFocus(true);
}

void UMD_GlassDrawingFocusComponent::HandleGlassEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	EndFocus(true);
}

void UMD_GlassDrawingFocusComponent::RefreshToolPresentation()
{
	if (AMD_Character* Character = GetCharacter()) Character->RefreshDrawingToolPresentation();
}

void UMD_GlassDrawingFocusComponent::OnRep_ActiveDesk()
{
	RefreshToolPresentation();
}

void UMD_GlassDrawingFocusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 소유 캐릭터 제거 시 남아 있는 로컬 화면 정리
	CloseDrawingWidget();
	EndFocus(true);
	DestroyCursorPen();
	if (IsValid(FocusCamera)) FocusCamera->Destroy();
	FocusCamera = nullptr;
	Super::EndPlay(EndPlayReason);
}
