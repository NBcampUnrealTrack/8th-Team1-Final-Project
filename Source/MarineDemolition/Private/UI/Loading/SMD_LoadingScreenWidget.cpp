#include "UI/Loading/SMD_LoadingScreenWidget.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MDLoadingScreen"

namespace
{
	// 기존 UMG 연출(Anim_TransitionEnd)의 실제 키프레임에서 옮겨온 값이다.
	// 좌표는 화면 중앙 기준 오프셋, 시간은 2.15초 루프 안의 초 단위다.
	// 손으로 맞춘 수치이므로 루프 길이를 바꾸면 타이밍도 함께 조정해야 한다.
	const FMD_LoadingBubbleMotion LoopBubbles[] =
	{
		{{24.0, 320.0}, {60.0, 60.0}, 0.6333f, 1.1500f, 1.8167f, 1.1167f, -45.0f, 2.0000f, -70.0f},
		{{60.0, 300.0}, {30.0, 30.0}, 0.5167f, 1.4500f, 1.7833f, 1.0833f, -55.0f, 1.9333f, -90.0f},
		{{ 2.0, 290.0}, {15.0, 15.0}, 0.7667f, 1.4167f, 1.9500f, 1.3500f, -40.0f, 2.0667f, -80.0f},
		{{40.0, 280.0}, { 7.0,  7.0}, 0.5167f, 1.4833f, 2.0000f, 1.3500f, -63.0f, 2.1000f, -103.0f},
		{{17.0, 275.0}, {23.0, 23.0}, 0.7833f, 1.4167f, 2.1333f, 1.4167f, -56.0f, 2.1333f, -96.0f},
	};

	/** [Start, End] 구간을 0~1로 매핑한 뒤 부드럽게 보간한다. */
	float SmoothRange(float Time, float Start, float End)
	{
		if (End <= Start) { return Time >= End ? 1.0f : 0.0f; }
		return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((Time - Start) / (End - Start), 0.0f, 1.0f));
	}
}

void SMD_LoadingScreenWidget::Construct(const FArguments& InArgs)
{
	VisualState = InArgs._VisualState;
	BubbleBrush.SetResourceObject(InArgs._BubbleTexture);
	BubbleBrush.ImageSize = FVector2D(32.0f, 32.0f);
	BubbleBrush.DrawAs = ESlateBrushDrawType::Image;

	TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24);
	TitleFont.LetterSpacing = 240;
	SubtitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
	SubtitleFont.LetterSpacing = 80;

	// Tick 대신 active timer로 무효화해야 게임 스레드가 막힌 동안에도
	// MoviePlayer의 로딩 스레드가 계속 repaint를 수행한다.
	SetCanTick(false);
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SMD_LoadingScreenWidget::RefreshPaint));
}

FVector2D SMD_LoadingScreenWidget::ComputeDesiredSize(float) const
{
	return MD_LoadingScreen::DesignSize;
}

int32 SMD_LoadingScreenWidget::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (!VisualState.IsValid()) { return LayerId; }

	const FMD_LoadingDesignSpace Space(AllottedGeometry);
	// 인트로는 두 단계다. 0.00~0.62초에 배경이 차오르고, 0.56~0.84초에 물방울과
	// 텍스트가 뒤이어 등장한다. 배경이 먼저 덮여야 트래블 순간이 가려진다.
	const float Time = static_cast<float>(VisualState->GetElapsedSeconds());
	const float OverallOpacity = VisualState->GetOverallOpacity();
	const float BackgroundOpacity = SmoothRange(Time, 0.0f, 0.62f) * OverallOpacity;
	const float TextOpacity = SmoothRange(Time, 0.56f, 0.84f) * OverallOpacity;

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None,
		FLinearColor(0.006f, 0.055f, 0.105f, BackgroundOpacity));

	// 물방울 하나가 빠르게 커진 뒤 사라지며 아래 반복 물방울로 자연스럽게 넘긴다.
	DrawBubble(Space, OutDrawElements, LayerId + 1, MD_LoadingScreen::DesignSize * 0.5f, FVector2D(64.0f, 64.0f),
		FMath::Lerp(0.30f, 18.0f, SmoothRange(Time, 0.02f, 0.54f)),
		0.92f * (1.0f - SmoothRange(Time, 0.40f, 0.60f)) * BackgroundOpacity);

	// 완료 표시는 독립된 성공 상태다. 진행 중을 암시하는 상승 물방울은 즉시 숨긴다.
	if (!VisualState->IsCompletionVisible())
	{
		DrawLoopBubbles(Space, OutDrawElements, LayerId + 2, Time, TextOpacity);
	}

	// 완료 연출은 교차 전환이다. 기존 문구가 위로 빠지고(LoadingExit) 완료 문구가
	// 아래에서 올라와 안착한다(Entrance). Pulse는 안착 순간의 scale 보정.
	const float CompletionElapsed = VisualState->GetCompletionElapsedSeconds();
	const float LoadingExit = SmoothRange(CompletionElapsed, 0.0f, 0.18f);
	const float Entrance = SmoothRange(CompletionElapsed, 0.12f, 0.38f);
	const float Pulse = FMath::Sin(FMath::Clamp(CompletionElapsed / 0.48f, 0.0f, 1.0f) * PI) * 0.10f;

	const FTextLayer TextLayers[] =
	{
		{LOCTEXT("LoadingTitle", "LOADING"), TitleFont,
			FLinearColor(0.90f, 0.99f, 1.0f, TextOpacity * (1.0f - LoadingExit)), 0.81f, 1.0f, -18.0f * LoadingExit},
		{VisualState->Subtitle, SubtitleFont,
			FLinearColor(0.65f, 0.90f, 1.0f, 0.95f * TextOpacity * (1.0f - LoadingExit)), 0.86f, 1.0f, -12.0f * LoadingExit},
		{LOCTEXT("CompleteTitle", "COMPLETE"), TitleFont,
			FLinearColor(0.62f, 1.0f, 0.88f, TextOpacity * Entrance), 0.81f, 0.86f + Entrance * 0.14f + Pulse,
			18.0f * (1.0f - Entrance)},
		{VisualState->CompletionSubtitle, SubtitleFont,
			FLinearColor(0.70f, 1.0f, 0.90f, 0.98f * TextOpacity * Entrance), 0.86f, 1.0f, 12.0f * (1.0f - Entrance)},
	};
	for (const FTextLayer& Layer : TextLayers)
	{
		DrawCenteredText(Space, OutDrawElements, LayerId + 3, Layer);
	}
	DrawCompletionCheck(Space, OutDrawElements, LayerId + 3, Entrance, TextOpacity);

	return LayerId + 3;
}

void SMD_LoadingScreenWidget::DrawBubble(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements,
	int32 LayerId, const FVector2D& DesignPosition, const FVector2D& BaseSize, float Scale, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER || !BubbleBrush.GetResourceObject()) { return; }

	const FVector2D DrawSize = BaseSize * Scale * Space.Scale;
	const FVector2D DrawPosition = Space.Point(DesignPosition) - DrawSize * 0.5f;
	FSlateDrawElement::MakeBox(DrawElements, LayerId,
		Space.Geometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(DrawPosition)),
		&BubbleBrush, ESlateDrawEffect::None, FLinearColor(0.82f, 0.98f, 1.0f, 0.96f * Opacity));
}

void SMD_LoadingScreenWidget::DrawLoopBubbles(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Time, float Opacity) const
{
	// 실제 로딩이 얼마나 걸리든 이 구간만 무한 반복한다.
	// +0.55초는 인트로 종료 시점과 물방울 등장 타이밍을 맞추기 위한 오프셋이다.
	const float LoopTime = FMath::Fmod(Time + 0.55f, static_cast<float>(MD_LoadingScreen::LoadingLoopDurationSeconds));
	for (const FMD_LoadingBubbleMotion& Bubble : LoopBubbles)
	{
		const float FadeIn = SmoothRange(LoopTime, Bubble.FadeInStart, Bubble.FadeInEnd);
		const float FadeOut = 1.0f - SmoothRange(LoopTime, Bubble.FadeInEnd, Bubble.FadeOutEnd);
		const float RiseY = LoopTime <= Bubble.RiseMidTime
			? FMath::Lerp(0.0f, Bubble.RiseMidY, SmoothRange(LoopTime, Bubble.FadeInStart, Bubble.RiseMidTime))
			: FMath::Lerp(Bubble.RiseMidY, Bubble.RiseEndY, SmoothRange(LoopTime, Bubble.RiseMidTime, Bubble.RiseEndTime));

		DrawBubble(Space, DrawElements, LayerId,
			MD_LoadingScreen::DesignSize * 0.5f + FVector2D(Bubble.Position.X, Bubble.Position.Y + RiseY),
			Bubble.Size, 1.0f, FadeIn * FadeOut * Opacity);
	}
}

void SMD_LoadingScreenWidget::DrawCompletionCheck(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Progress, float Opacity) const
{
	if (Progress <= KINDA_SMALL_NUMBER || Opacity <= KINDA_SMALL_NUMBER) { return; }

	const FVector2D Center = Space.Point(
		FVector2D(MD_LoadingScreen::DesignSize.X * 0.5f, MD_LoadingScreen::DesignSize.Y * 0.745f));
	// 체크 표시를 두 획으로 나눠 Progress 0.0~0.5 / 0.5~1.0 구간에 걸쳐 그려 나간다.
	const FVector2D Joints[] =
	{
		Center + FVector2D(-15.0f, 0.0f) * Space.Scale,
		Center + FVector2D(-4.0f, 11.0f) * Space.Scale,
		Center + FVector2D(20.0f, -15.0f) * Space.Scale,
	};
	const FLinearColor Color(0.66f, 1.0f, 0.88f, Opacity * Progress);

	for (int32 Stroke = 0; Stroke < 2; ++Stroke)
	{
		const float StrokeProgress = FMath::Clamp((Progress - 0.5f * Stroke) * 2.0f, 0.0f, 1.0f);
		if (StrokeProgress <= 0.0f) { break; }

		const TArray<FVector2d> Line =
		{
			FVector2d(Joints[Stroke]),
			FVector2d(FMath::Lerp(Joints[Stroke], Joints[Stroke + 1], StrokeProgress)),
		};
		FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), Line,
			ESlateDrawEffect::None, Color, true, 3.0f * Space.Scale);
	}
}

void SMD_LoadingScreenWidget::DrawCenteredText(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, const FTextLayer& Layer) const
{
	if (Layer.Color.A <= KINDA_SMALL_NUMBER) { return; }

	FSlateFontInfo ScaledFont = Layer.Font;
	ScaledFont.Size = FMath::Max(1, FMath::RoundToInt(
		static_cast<float>(Layer.Font.Size) * Space.Scale * Layer.TextScale));

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D TextSize = FontMeasure->Measure(Layer.Text, ScaledFont);
	const FVector2D Anchor = Space.Point(FVector2D(
		MD_LoadingScreen::DesignSize.X * 0.5f,
		MD_LoadingScreen::DesignSize.Y * Layer.AnchorY + Layer.OffsetY));

	FSlateDrawElement::MakeText(DrawElements, LayerId,
		Space.Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Anchor - TextSize * 0.5f)),
		Layer.Text, ScaledFont, ESlateDrawEffect::None, Layer.Color);
}

// 연출 값이 전부 경과 시간에서 파생되므로 상태 갱신 없이 무효화만 걸면 된다.
EActiveTimerReturnType SMD_LoadingScreenWidget::RefreshPaint(double, float)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
