// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Game/MD_ScrapeFocusReticleWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "GameFramework/PlayerController.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SWidget.h"

#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Util/MD_LogUtility.h"

static void SetReticleVisibility(UImage* Image, const bool bVisible)
{
	if (!IsValid(Image))
	{
		return;
	}

	// 숨겨도 디자이너의 레이아웃 공간을 유지한다. Collapsed는 주변 배치를 바꿀 수 있다.
	const ESlateVisibility Visibility = bVisible
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Hidden;
	if (Image->GetVisibility() != Visibility)
	{
		Image->SetVisibility(Visibility);
	}
}

static bool FindReticleGeometry(
	const TSharedPtr<SWidget>& Root,
	const FGeometry& RootGeometry,
	const TSharedPtr<SWidget>& Image,
	FGeometry& OutGeometry)
{
	if (!Root.IsValid() || !Image.IsValid())
	{
		return false;
	}

	// 패널 타입 대신 실제 Slate 부모 경로를 사용한다. 보통 깊이에서는 힙 할당이 없다.
	TArray<TSharedPtr<SWidget>, TInlineAllocator<16>> Path;
	TSharedPtr<SWidget> Current = Image;
	while (Current.IsValid() && Current != Root)
	{
		Path.Add(Current);
		Current = Current->GetParentWidget();
	}
	if (Current != Root)
	{
		return false;
	}

	// 지난 프레임의 Image 캐시를 사용하면 이동량 피드백과 resize 시 오차가 생긴다.
	// Tick으로 받은 현재 Geometry에서 해당 경로만 다시 배치해 실제 위치를 구한다.
	OutGeometry = RootGeometry;
	for (int32 Index = Path.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<SWidget>& Parent = Index == Path.Num() - 1 ? Root : Path[Index + 1];
		FArrangedChildren Children(EVisibility::All);
		Parent->ArrangeChildren(OutGeometry, Children);
		const int32 ChildIndex = Children.IndexOfByPredicate(
			[&Path, Index](const FArrangedWidget& Child) { return Child.Widget == Path[Index]; });
		if (ChildIndex == INDEX_NONE)
		{
			return false;
		}
		OutGeometry = Children[ChildIndex].Geometry;
	}
	return true;
}

static bool MoveReticleImage(
	UImage* Image,
	const TSharedPtr<SWidget>& Root,
	const FGeometry& RootGeometry,
	const FVector2D& DesktopPosition)
{
	FGeometry ImageGeometry;
	if (!FindReticleGeometry(Root, RootGeometry, Image->GetCachedWidget(), ImageGeometry))
	{
		return false;
	}

	const FVector2D ImageSize = ImageGeometry.GetLocalSize();
	const float Determinant = ImageGeometry.GetAccumulatedRenderTransform().GetMatrix().Determinant();
	if (ImageSize.X <= UE_SMALL_NUMBER || ImageSize.Y <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(Determinant) || FMath::IsNearlyZero(Determinant, UE_SMALL_NUMBER))
	{
		return false;
	}

	const FWidgetTransform CurrentTransform = Image->GetRenderTransform();
	const FVector2D LocalDelta = ImageGeometry.AbsoluteToLocal(DesktopPosition) - ImageSize * 0.5;
	// AbsoluteToLocal은 Image 자신의 회전/스케일까지 역변환한다.
	// Translation에는 그 선형 변환을 다시 적용해야 부모 공간에서 정확한 이동량이 된다.
	const FVector2D Translation = CurrentTransform.Translation
		+ TransformVector(CurrentTransform.ToSlateRenderTransform(), LocalDelta);
	if (!FMath::IsFinite(Translation.X) || !FMath::IsFinite(Translation.Y))
	{
		return false;
	}
	if (!CurrentTransform.Translation.Equals(Translation))
	{
		Image->SetRenderTranslation(Translation);
	}
	return true;
}

UMD_ScrapeFocusReticleWidget::UMD_ScrapeFocusReticleWidget(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
}

void UMD_ScrapeFocusReticleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetIsFocusable(false);
	ForceVolatile(false);
	SetReticleVisibility(Img_CenterReticle, false);
	SetReticleVisibility(Img_FocusReticle, false);

	if (!IsValid(Img_CenterReticle) || !IsValid(Img_CenterReticle->GetBrush().GetResourceObject()))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s: Place Img_CenterReticle in the Widget Blueprint and assign its Brush texture."), *GetName());
	}
	if (!IsValid(Img_FocusReticle) || !IsValid(Img_FocusReticle->GetBrush().GetResourceObject()))
	{
		UE_LOG(MarineDemolition, Warning,
			TEXT("%s: Place Img_FocusReticle in the Widget Blueprint and assign its Brush texture."), *GetName());
	}
}

void UMD_ScrapeFocusReticleWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateReticles(MyGeometry);
}

void UMD_ScrapeFocusReticleWidget::UpdateReticles(const FGeometry& MyGeometry)
{
	APlayerController* const PlayerController = GetOwningPlayer();
	const AMD_Character* const Character = IsValid(PlayerController)
		? Cast<AMD_Character>(PlayerController->GetPawn())
		: nullptr;
	SetReticleVisibility(Img_CenterReticle, IsValid(Character)
		&& IsValid(Img_CenterReticle)
		&& IsValid(Img_CenterReticle->GetBrush().GetResourceObject()));

	const UMD_ScrapeToolComponent* const ScrapeTool = IsValid(Character)
		? Character->GetScrapeToolComponent()
		: nullptr;
	if (!IsValid(ScrapeTool) || !ScrapeTool->IsScrapeFocusViewActive()
		|| !IsValid(Img_FocusReticle) || !IsValid(Img_FocusReticle->GetBrush().GetResourceObject()))
	{
		SetReticleVisibility(Img_FocusReticle, false);
		return;
	}

	const FVector2D Pointer = ScrapeTool->GetScrapeFocusPointerNormalized();
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PlayerController);
	const FVector2D PlayerSize = PlayerGeometry.GetLocalSize();
	if (!FMath::IsFinite(Pointer.X) || !FMath::IsFinite(Pointer.Y)
		|| PlayerSize.X <= UE_SMALL_NUMBER || PlayerSize.Y <= UE_SMALL_NUMBER)
	{
		SetReticleVisibility(Img_FocusReticle, false);
		return;
	}

	// 두 Geometry 모두 Tick의 desktop 좌표계다. 부모 패널의 종류·위치·여백에 의존하지 않는다.
	const FVector2D DesktopPosition = PlayerGeometry.LocalToAbsolute(
		FVector2D(FMath::Clamp(Pointer.X, 0.0, 1.0), FMath::Clamp(Pointer.Y, 0.0, 1.0)) * PlayerSize);
	if (!MoveReticleImage(Img_FocusReticle, GetCachedWidget(), MyGeometry, DesktopPosition))
	{
		SetReticleVisibility(Img_FocusReticle, false);
		return;
	}

	const FLinearColor ReticleColor =
		ScrapeTool->GetScrapeFocusState() == EMD_ScrapeFocusState::FocusPending
			? PendingColor
			: (ScrapeTool->IsScrapeFocusReady() && ScrapeTool->IsScrapeFocusAimValid()
				? ActionableColor
				: InvalidColor);
	if (Img_FocusReticle->GetColorAndOpacity() != ReticleColor)
	{
		Img_FocusReticle->SetColorAndOpacity(ReticleColor);
	}
	SetReticleVisibility(Img_FocusReticle, true);
}
