#include "UI/Loading/SMD_LoadingScreenWidget.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderTransform.h"
#include "Rendering/SlateRenderer.h"
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

	// 진행 트랙의 디자인 좌표. 텍스트/물방울보다 위쪽에 두어 서로 겹치지 않게 한다.
	constexpr float VoyageTrackStartX = 300.0f;
	constexpr float VoyageTrackEndX = 1620.0f;
	constexpr float BackgroundHorizonY = 665.0f;
	constexpr float VoyageTrackY = 705.0f;
	constexpr float VoyageWaveAmplitude = 7.0f;
	constexpr int32 VoyageSegmentCount = 72;

	/**
	 * 항로의 세로 흔들림. 트랙·항로 표식·종점 깃발·배가 모두 같은 파형 위에 놓여야 하므로
	 * 네 곳에서 같은 식을 되풀이하지 않고 여기서만 계산한다. Alpha는 트랙 시작 0, 끝 1.
	 */
	float VoyageWaveOffset(float Alpha, float Phase)
	{
		return FMath::Sin(Alpha * 8.0f * PI + Phase) * VoyageWaveAmplitude
			+ FMath::Sin(Alpha * 3.0f * PI - Phase * 0.7f) * 2.5f;
	}

	// OnPaint의 층 배치. DrawVoyageProgress가 자기 기준으로 VoyageLayerSpan개를 쓰므로 텍스트는
	// 그 위에서 시작해야 하고, OnPaint는 실제로 사용한 마지막 층을 그대로 돌려줘야 한다.
	// 돌려준 값보다 위에 그리면 뒤이어 그려질 형제 위젯이 이 위젯에 가려진다.
	constexpr int32 SkyLayer = 0;
	constexpr int32 CloudLayer = 1;
	constexpr int32 IslandLayer = 2;
	constexpr int32 SwellLayer = 3;
	constexpr int32 IntroBubbleLayer = 4;
	constexpr int32 LoopBubbleLayer = 5;
	constexpr int32 VoyageLayer = 6;
	constexpr int32 VoyageLayerSpan = 6;
	constexpr int32 TextLayer = VoyageLayer + VoyageLayerSpan;

	// 배경은 항로(=수평선)를 기준으로 위가 하늘, 아래가 바다다. 상단의 옅은 하늘색이
	// 수평선의 복숭아빛으로 이어지고, 바다는 맑은 청색으로 식도록 해 일출 직후의 밝기를 만든다.
	// 섬은 역광 실루엣으로 남겨 밝아진 수평선 앞에서도 형태가 또렷하게 읽히게 한다.
	const FLinearColor SkyTopColor(0.470f, 0.710f, 0.850f);
	const FLinearColor SkyMidColor(0.690f, 0.810f, 0.850f);
	const FLinearColor SkyGlowColor(0.950f, 0.770f, 0.590f);
	const FLinearColor SkyHorizonColor(1.000f, 0.590f, 0.380f);
	const FLinearColor SeaHorizonColor(0.430f, 0.620f, 0.740f);
	const FLinearColor SeaNearColor(0.290f, 0.570f, 0.710f);
	const FLinearColor SeaDeepColor(0.115f, 0.365f, 0.560f);
	const FLinearColor HorizonLineColor(0.310f, 0.430f, 0.570f);
	const FLinearColor IslandColor(0.060f, 0.080f, 0.115f);
	const FLinearColor CloudColor(0.960f, 0.910f, 0.830f);

	/** 진행에 따라 흐르는 배경의 총 이동량(디자인 px). 레이어마다 배율을 달리해 시차를 만든다. */
	constexpr float BackgroundDriftSpan = 260.0f;

	/** 화면 밖으로 나간 배경 요소를 반대편에서 다시 들여보낸다. */
	float WrapDesignX(float X, float Margin)
	{
		const float Span = static_cast<float>(MD_LoadingScreen::DesignSize.X) + Margin * 2.0f;
		return FMath::Fmod(FMath::Fmod(X + Margin, Span) + Span, Span) - Margin;
	}

	/**
	 * 구름 한 덩어리. 두꺼운 선으로 그리면 모서리가 각져 사각형으로 보이므로, 봉우리 세 개를
	 * 겹친 윤곽을 만들어 섬과 같은 삼각형 스트립으로 채운다. 양 끝에서 두께가 0으로 수렴해
	 * 잘린 자국이 남지 않는다.
	 */
	struct FCloudBank
	{
		float X, Y, HalfWidth, Height, Alpha, ParallaxScale;
		float LumpOffset[3], LumpWidth[3], LumpAmp[3];
	};
	const FCloudBank CloudBanks[] =
	{
		{  300.0f, 224.0f, 250.0f, 74.0f, 0.115f, 0.35f,
			{-0.46f, 0.04f, 0.52f}, {0.40f, 0.54f, 0.36f}, {0.74f, 1.00f, 0.62f} },
		{  820.0f, 168.0f, 300.0f, 88.0f, 0.100f, 0.28f,
			{-0.30f, 0.18f, 0.62f}, {0.46f, 0.62f, 0.34f}, {0.88f, 0.70f, 0.50f} },
		{ 1340.0f, 252.0f, 275.0f, 80.0f, 0.120f, 0.42f,
			{-0.52f, 0.00f, 0.44f}, {0.34f, 0.58f, 0.44f}, {0.58f, 1.00f, 0.76f} },
		{ 1760.0f, 178.0f, 210.0f, 62.0f, 0.090f, 0.33f,
			{-0.34f, 0.22f, 0.58f}, {0.44f, 0.50f, 0.36f}, {0.80f, 0.86f, 0.54f} },
		{  560.0f, 430.0f, 330.0f, 34.0f, 0.060f, 0.55f,
			{-0.40f, 0.10f, 0.56f}, {0.50f, 0.66f, 0.42f}, {0.70f, 1.00f, 0.64f} },
		{ 1180.0f, 476.0f, 290.0f, 28.0f, 0.055f, 0.62f,
			{-0.28f, 0.16f, 0.60f}, {0.48f, 0.58f, 0.40f}, {0.82f, 0.92f, 0.58f} },
	};

	/** 구름 윤곽. U는 -1~1이고, 봉우리 중 가장 높은 값을 쓴 뒤 양 끝을 0으로 눌러 닫는다. */
	float CloudProfileAt(float U, const FCloudBank& Cloud)
	{
		float Height = 0.0f;
		for (int32 Lump = 0; Lump < 3; ++Lump)
		{
			const float Distance = (U - Cloud.LumpOffset[Lump]) / Cloud.LumpWidth[Lump];
			Height = FMath::Max(Height, Cloud.LumpAmp[Lump] * FMath::Exp(-Distance * Distance));
		}
		return Height * FMath::Max(0.0f, 1.0f - FMath::Pow(FMath::Abs(U), 6.0f));
	}

	/** 수평선에 걸린 바위섬 하나. Height는 수평선 위로 솟는 높이다. */
	struct FIslandShape
	{
		float CentreX, HalfWidth, Height, Sharpness, Falloff, PeakShift, Alpha, ParallaxScale;
		float RidgeFrequency, RidgeAmount;
	};
	const FIslandShape IslandShapes[] =
	{
		{  270.0f, 300.0f, 132.0f, 3.6f, 0.38f, -0.22f, 0.92f, 1.00f, 5.0f, 0.13f },
		{  880.0f, 210.0f,  88.0f, 4.4f, 0.34f,  0.30f, 0.66f, 0.72f, 7.0f, 0.09f },
		{ 1520.0f, 340.0f, 158.0f, 3.2f, 0.42f,  0.16f, 0.96f, 1.20f, 4.0f, 0.16f },
	};

	/**
	 * 섬 실루엣의 세로 단면. U는 왼쪽 끝 -1, 오른쪽 끝 1이고 0~1을 돌려준다.
	 * Sharpness를 키우면 옆면이 서고 윗면이 평평해져 게임 안의 깎아지른 바위에 가까워지고,
	 * Ridge 항이 능선을 울퉁불퉁하게 만들어 매끈한 언덕처럼 보이지 않게 한다.
	 */
	float IslandHeightAt(float U, const FIslandShape& Island)
	{
		const float Shifted = FMath::Clamp((U - Island.PeakShift) / (1.0f - FMath::Abs(Island.PeakShift)),
			-1.0f, 1.0f);
		const float Body = FMath::Pow(FMath::Max(0.0f, 1.0f - FMath::Pow(FMath::Abs(Shifted), Island.Sharpness)),
			Island.Falloff);
		const float Ridge = Island.RidgeAmount * FMath::Sin((Shifted + 1.0f) * Island.RidgeFrequency * PI);
		return FMath::Max(0.0f, Body + Ridge * Body);
	}

	/**
	 * Slate에는 채워진 다각형을 그리는 기본 요소가 없다. 두꺼운 선을 여러 개 이어 붙여 채우면
	 * 알파가 겹치는 자리마다 이음매가 보이므로, 정점을 직접 만들어 삼각형 스트립으로 넘긴다.
	 * 위/아래 정점을 번갈아 넣고 마지막에 Draw를 부르면 된다.
	 */
	struct FSlateShapeStrip
	{
		explicit FSlateShapeStrip(const FGeometry& InGeometry)
			: RenderTransform(InGeometry.ToPaintGeometry().GetAccumulatedRenderTransform()) {}

		void Add(const FVector2D& Position, const FLinearColor& Color)
		{
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform,
				FVector2f(Position), FVector2f(0.5f, 0.5f), Color.ToFColor(false)));
		}

		void Draw(FSlateWindowElementList& DrawElements, int32 LayerId)
		{
			if (Vertices.Num() < 4) { return; }

			TArray<SlateIndex> Indices;
			Indices.Reserve((Vertices.Num() / 2 - 1) * 6);
			for (int32 Index = 0; Index + 3 < Vertices.Num(); Index += 2)
			{
				const SlateIndex Base = static_cast<SlateIndex>(Index);
				Indices.Append({Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2});
			}

			// 흰 브러시를 정점 색으로만 칠한다. 텍스처가 필요 없는 순수 도형이다.
			const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
			const FSlateResourceHandle Handle =
				FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
			FSlateDrawElement::MakeCustomVerts(DrawElements, LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
		}

		FSlateRenderTransform RenderTransform;
		TArray<FSlateVertex> Vertices;
	};
}

void SMD_LoadingScreenWidget::Construct(const FArguments& InArgs)
{
	VisualState = InArgs._VisualState;
	BubbleBrush.SetResourceObject(InArgs._BubbleTexture);
	BubbleBrush.ImageSize = FVector2D(32.0f, 32.0f);
	BubbleBrush.DrawAs = ESlateBrushDrawType::Image;
	ShipBrush.SetResourceObject(InArgs._ShipTexture);
	ShipBrush.ImageSize = FVector2D(462.0f, 146.0f);
	ShipBrush.DrawAs = ESlateBrushDrawType::Image;
	WaveBrush.SetResourceObject(InArgs._WaveTexture);
	WaveBrush.ImageSize = FVector2D(1920.0f, 294.0f);
	WaveBrush.DrawAs = ESlateBrushDrawType::Image;

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

	// 배경은 하늘/수평선/바다 그라데이션 위에 구름 띠와 수평선 아래 너울만 얹는다.
	// 항로와 배는 배경 수평선보다 조금 아래에 두어 바다 위를 이동하는 모습으로 읽히게 한다.
	const float Progress = VisualState->GetVisualProgress();
	const float BackgroundDrift = Progress * BackgroundDriftSpan;
	DrawSkyAndSea(Space, OutDrawElements, LayerId + SkyLayer, BackgroundOpacity);
	DrawCloudBands(Space, OutDrawElements, LayerId + CloudLayer, BackgroundDrift + Time * 6.0f,
		BackgroundOpacity);
	DrawSeaSwell(Space, OutDrawElements, LayerId + SwellLayer, BackgroundDrift * 1.6f + Time * 18.0f,
		BackgroundOpacity);

	// 물방울 하나가 빠르게 커진 뒤 사라지며 아래 반복 물방울로 자연스럽게 넘긴다.
	DrawBubble(Space, OutDrawElements, LayerId + IntroBubbleLayer,
		MD_LoadingScreen::DesignSize * 0.5f, FVector2D(64.0f, 64.0f),
		FMath::Lerp(0.30f, 18.0f, SmoothRange(Time, 0.02f, 0.54f)),
		0.92f * (1.0f - SmoothRange(Time, 0.40f, 0.60f)) * BackgroundOpacity);

	// 완료 표시는 독립된 성공 상태다. 진행 중을 암시하는 상승 물방울은 즉시 숨긴다.
	if (!VisualState->IsCompletionVisible())
	{
		DrawLoopBubbles(Space, OutDrawElements, LayerId + LoopBubbleLayer, Time, TextOpacity);
	}

	// PEAK의 이동형 로딩 인디케이터를 이 프로젝트의 바다/작업선 모티프로 번역한다.
	// 트랙은 항상 길게 남고, 밝아진 항로 끝을 배가 따라가므로 진행 방향을 즉시 읽을 수 있다.
	DrawVoyageProgress(Space, OutDrawElements, LayerId + VoyageLayer, Time, Progress, TextOpacity);

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
		DrawCenteredText(Space, OutDrawElements, LayerId + TextLayer, Layer);
	}
	DrawCompletionCheck(Space, OutDrawElements, LayerId + TextLayer, Entrance, TextOpacity);

	return LayerId + TextLayer;
}

// 하늘과 바다를 한 장의 세로 그라데이션으로 그린다. MakeGradient 대신 정점 색을 직접 지정하는
// 이유: 그라데이션 요소의 방향 해석이 헷갈리는 데다, 수평선을 기준으로 위아래 색을 따로
// 잡으려면 중간 색 지점이 여러 개 필요하기 때문이다. 화면 전체를 덮어야 하드 트래블 순간
// 이전 월드가 비치지 않으므로, 디자인 좌표가 아니라 위젯 로컬 크기 전체에 그린다.
void SMD_LoadingScreenWidget::DrawSkyAndSea(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	const FVector2D LocalSize = Space.Geometry.GetLocalSize();
	const float HorizonY = static_cast<float>(Space.Point(FVector2D(0.0f, BackgroundHorizonY)).Y);

	// 수평선 직전/직후의 색 지점을 좁게 붙여 하늘과 바다 사이가 넓게 뿌옇게 섞이지 않게 한다.
	// 나머지 색 지점 사이는 GPU가 선형 보간한다.
	const TPair<float, FLinearColor> Bands[] =
	{
		{0.0f, SkyTopColor},
		{HorizonY * 0.52f, SkyMidColor},
		{HorizonY * 0.86f, SkyGlowColor},
		{HorizonY - 3.0f * Space.Scale, SkyHorizonColor},
		{HorizonY + 3.0f * Space.Scale, SeaHorizonColor},
		{HorizonY + 95.0f * Space.Scale, SeaNearColor},
		{static_cast<float>(LocalSize.Y), SeaDeepColor},
	};

	FSlateShapeStrip Strip(Space.Geometry);
	for (const TPair<float, FLinearColor>& Band : Bands)
	{
		const FLinearColor Color(Band.Value.R, Band.Value.G, Band.Value.B, Opacity);
		Strip.Add(FVector2D(0.0f, Band.Key), Color);
		Strip.Add(FVector2D(LocalSize.X, Band.Key), Color);
	}
	Strip.Draw(DrawElements, LayerId);

	// 하늘과 바다의 경계가 분명하도록 차가운 푸른 선을 한 겹 얹는다.
	const TArray<FVector2D> HorizonLine =
	{
		FVector2D(0.0f, HorizonY),
		FVector2D(LocalSize.X, HorizonY),
	};
	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), HorizonLine,
		ESlateDrawEffect::None,
		FLinearColor(HorizonLineColor.R, HorizonLineColor.G, HorizonLineColor.B, 0.72f * Opacity),
		true, FMath::Max(1.0f, 2.0f * Space.Scale));
}

// 구름 덩어리. 아래쪽은 수평선 빛을 받아 밝고 위로 갈수록 옅어지도록 정점 알파를 달리 준다.
// 위/아래 윤곽이 양 끝에서 같은 높이로 모이므로 덩어리가 저절로 닫혀 잘린 자국이 없다.
void SMD_LoadingScreenWidget::DrawCloudBands(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Drift, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	constexpr int32 ProfileSteps = 44;
	for (const FCloudBank& Cloud : CloudBanks)
	{
		const float CentreX = WrapDesignX(Cloud.X - Drift * Cloud.ParallaxScale, 520.0f);
		const FLinearColor TopColor(CloudColor.R, CloudColor.G, CloudColor.B, Cloud.Alpha * 0.45f * Opacity);
		const FLinearColor BottomColor(CloudColor.R, CloudColor.G, CloudColor.B, Cloud.Alpha * Opacity);

		FSlateShapeStrip Strip(Space.Geometry);
		for (int32 Step = 0; Step <= ProfileSteps; ++Step)
		{
			const float U = -1.0f + 2.0f * Step / ProfileSteps;
			const float Profile = CloudProfileAt(U, Cloud);
			const float X = CentreX + U * Cloud.HalfWidth;
			Strip.Add(Space.Point(FVector2D(X, Cloud.Y - Cloud.Height * Profile)), TopColor);
			Strip.Add(Space.Point(FVector2D(X, Cloud.Y + Cloud.Height * Profile * 0.30f)), BottomColor);
		}
		Strip.Draw(DrawElements, LayerId);
	}
}

// 수평선에 걸린 바위섬 실루엣. 섬 하나가 삼각형 스트립 한 덩어리라 알파가 겹치지 않는다.
void SMD_LoadingScreenWidget::DrawIslands(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Drift, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	constexpr int32 ProfileSteps = 48;
	// 수평선보다 조금 아래까지 내려 그려야 항로 선과 섬 밑동 사이에 틈이 보이지 않는다.
	constexpr float SkirtBelowHorizon = 10.0f;

	for (const FIslandShape& Island : IslandShapes)
	{
		const float CentreX = WrapDesignX(Island.CentreX - Drift * Island.ParallaxScale, 520.0f);
		const FLinearColor Color(IslandColor.R, IslandColor.G, IslandColor.B, Island.Alpha * Opacity);

		FSlateShapeStrip Strip(Space.Geometry);
		for (int32 Step = 0; Step <= ProfileSteps; ++Step)
		{
			const float U = -1.0f + 2.0f * Step / ProfileSteps;
			const float X = CentreX + U * Island.HalfWidth;
			Strip.Add(Space.Point(FVector2D(X, VoyageTrackY - Island.Height * IslandHeightAt(U, Island))), Color);
			Strip.Add(Space.Point(FVector2D(X, VoyageTrackY + SkirtBelowHorizon)), Color);
		}
		Strip.Draw(DrawElements, LayerId);
	}
}

// 수평선 아래 너울. 텍스처 한 장을 두 번 이어 붙여 옆으로 흘리고, 문구 아래쪽에만 두어
// LOADING/부제와 겹치지 않게 한다.
void SMD_LoadingScreenWidget::DrawSeaSwell(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Drift, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER || !WaveBrush.GetResourceObject()) { return; }

	struct FSwellBand { float Y, Height, Speed, Alpha; };
	const FSwellBand Bands[] =
	{
		{ 966.0f, 132.0f, 0.55f, 0.30f },
		{ 1016.0f, 150.0f, 1.00f, 0.55f },
	};

	const float DesignWidth = static_cast<float>(MD_LoadingScreen::DesignSize.X);
	for (const FSwellBand& Band : Bands)
	{
		const float Offset = FMath::Fmod(FMath::Fmod(Drift * Band.Speed, DesignWidth) + DesignWidth, DesignWidth);
		const FVector2D BandSize(DesignWidth * Space.Scale, Band.Height * Space.Scale);
		// 두 장을 폭만큼 띄워 놓으면 한 장이 왼쪽으로 빠져나가는 동안 다른 한 장이 들어온다.
		for (int32 Copy = 0; Copy < 2; ++Copy)
		{
			const FVector2D Position = Space.Point(
				FVector2D(-Offset + DesignWidth * Copy, Band.Y));
			FSlateDrawElement::MakeBox(DrawElements, LayerId,
				Space.Geometry.ToPaintGeometry(BandSize, FSlateLayoutTransform(Position)),
				&WaveBrush, ESlateDrawEffect::None,
				FLinearColor(0.400f, 0.720f, 0.800f, Band.Alpha * Opacity));
		}
	}
}

void SMD_LoadingScreenWidget::DrawVoyageProgress(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Time, float Progress, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	// LayerId 기준: +0 잔물결/트랙, +1 항적 글로우, +2 항적, +3 항로 표식, +4 배, +5 종점 깃발.
	static_assert(VoyageLayerSpan == 6, "여기서 쓰는 층 수와 VoyageLayerSpan이 어긋났다");

	const float WavePhase = Time * 1.25f;
	// 디자인 좌표 Alpha(0~1) 위의 한 점을 실제 위젯 좌표로 옮긴다.
	const auto TrackPointAt = [&Space, WavePhase](float Alpha)
	{
		return Space.Point(FVector2D(FMath::Lerp(VoyageTrackStartX, VoyageTrackEndX, Alpha),
			VoyageTrackY + VoyageWaveOffset(Alpha, WavePhase)));
	};

	const int32 CompletedSegments =
		FMath::Clamp(FMath::FloorToInt(Progress * VoyageSegmentCount), 0, VoyageSegmentCount);
	const float SegmentRemainder = Progress * VoyageSegmentCount - CompletedSegments;

	TArray<FVector2d> TrackPoints;
	TArray<FVector2d> RipplePoints;
	TArray<FVector2d> CompletedPoints;
	TrackPoints.Reserve(VoyageSegmentCount + 1);
	RipplePoints.Reserve(VoyageSegmentCount + 1);
	CompletedPoints.Reserve(CompletedSegments + 2);
	for (int32 Index = 0; Index <= VoyageSegmentCount; ++Index)
	{
		const FVector2D Point = TrackPointAt(static_cast<float>(Index) / VoyageSegmentCount);
		TrackPoints.Add(FVector2d(Point));
		RipplePoints.Add(FVector2d(Point + FVector2D(0.0f, 12.0f) * Space.Scale));
		if (Index <= CompletedSegments) { CompletedPoints.Add(FVector2d(Point)); }
	}

	// 세그먼트 사이에 걸친 진행분까지 항적을 이어 붙여야 배가 항적 끝에서 떨어져 보이지 않는다.
	if (CompletedSegments < VoyageSegmentCount && SegmentRemainder > KINDA_SMALL_NUMBER)
	{
		CompletedPoints.Add(FMath::Lerp(TrackPoints[CompletedSegments], TrackPoints[CompletedSegments + 1],
			static_cast<double>(SegmentRemainder)));
	}

	const FLinearColor TrackColor(0.32f, 0.70f, 0.80f, 0.48f * Opacity);
	const FLinearColor WakeGlow(0.25f, 0.93f, 1.0f, 0.16f * Opacity);
	const FLinearColor WakeColor(0.72f, 0.98f, 1.0f, 0.92f * Opacity);
	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), RipplePoints,
		ESlateDrawEffect::None, FLinearColor(0.18f, 0.52f, 0.65f, 0.13f * Opacity), true, 2.0f * Space.Scale);
	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), TrackPoints,
		ESlateDrawEffect::None, TrackColor, true, 2.0f * Space.Scale);
	if (CompletedPoints.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 1, Space.Geometry.ToPaintGeometry(), CompletedPoints,
			ESlateDrawEffect::None, WakeGlow, true, 11.0f * Space.Scale);
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 2, Space.Geometry.ToPaintGeometry(), CompletedPoints,
			ESlateDrawEffect::None, WakeColor, true, 3.0f * Space.Scale);
	}

	// 25% 단위의 작은 항로 표식은 트랙의 길이와 진행 방향을 읽게 하되 텍스트보다 눈에 띄지 않게 둔다.
	for (int32 Marker = 0; Marker <= 4; ++Marker)
	{
		const FVector2D Center = TrackPointAt(Marker / 4.0f);
		const TArray<FVector2d> MarkerLine =
		{
			FVector2d(Center + FVector2D(0.0f, -5.0f) * Space.Scale),
			FVector2d(Center + FVector2D(0.0f, 5.0f) * Space.Scale),
		};
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 3, Space.Geometry.ToPaintGeometry(), MarkerLine,
			ESlateDrawEffect::None, FLinearColor(0.72f, 0.96f, 1.0f, 0.42f * Opacity), true, 2.0f * Space.Scale);
	}

	const FVector2D Destination = TrackPointAt(1.0f);
	const TArray<FVector2d> DestinationBeacon =
	{
		FVector2d(Destination + FVector2D(0.0f, 5.0f) * Space.Scale),
		FVector2d(Destination + FVector2D(0.0f, -24.0f) * Space.Scale),
		FVector2d(Destination + FVector2D(9.0f, -19.0f) * Space.Scale),
		FVector2d(Destination + FVector2D(0.0f, -14.0f) * Space.Scale),
	};
	FSlateDrawElement::MakeLines(DrawElements, LayerId + 5, Space.Geometry.ToPaintGeometry(), DestinationBeacon,
		ESlateDrawEffect::None, FLinearColor(0.72f, 0.96f, 1.0f, 0.72f * Opacity), true, 2.0f * Space.Scale);

	// 텍스처가 지정되지 않았으면 트랙만 남기고 배는 생략한다.
	if (!ShipBrush.GetResourceObject()) { return; }

	// 기준점은 선체 바닥에서 조금 뒤(0.64)에 둔다. 나머지 0.36이 앞으로 남아 뱃머리가 항적 끝에
	// 살짝 걸치고 선체는 뒤로 끌린다.
	const float Bob = FMath::Sin(Time * 2.8f) * 3.0f;
	const FVector2D ShipSize(218.0f * Space.Scale, 69.0f * Space.Scale);
	const FVector2D ShipBottom = TrackPointAt(Progress) + FVector2D(0.0f, Bob) * Space.Scale;
	const FVector2D ShipPosition = ShipBottom - FVector2D(ShipSize.X * 0.64f, ShipSize.Y * 0.88f);

	// T_Ship 원본은 메인 메뉴용이라 뱃머리가 왼쪽을 본다. 항로는 왼→오른쪽으로 나아가므로
	// 그대로 쓰면 배가 뒤로 가는 꼴이다. 피벗이 상자 중심(정규화 0.5, 0.5)이라 좌우만 뒤집히고
	// 그리는 위치는 그대로다.
	static const FSlateRenderTransform ShipFacingRight(FScale2D(-1.0f, 1.0f));
	FSlateDrawElement::MakeBox(DrawElements, LayerId + 4,
		Space.Geometry.ToPaintGeometry(ShipSize, FSlateLayoutTransform(ShipPosition), ShipFacingRight),
		&ShipBrush, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
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
