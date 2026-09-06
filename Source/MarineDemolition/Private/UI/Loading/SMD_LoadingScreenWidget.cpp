#include "UI/Loading/SMD_LoadingScreenWidget.h"

#include "Engine/Texture2D.h"
#include "Engine/Font.h"
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
	/** [Start, End] 구간을 0~1로 매핑한 뒤 부드럽게 보간한다. */
	float SmoothRange(float Time, float Start, float End)
	{
		if (End <= Start) { return Time >= End ? 1.0f : 0.0f; }
		return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((Time - Start) / (End - Start), 0.0f, 1.0f));
	}

	/** 팔레트 색은 알파 없이 정의하고, 그릴 때 현재 페이드 값만 덮어씌운다. */
	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Alpha);
	}

	/** 중심에서의 거리 Distance(반지름 1 기준)에 대한 반원 높이. 구름 로브의 기본 형태다. */
	float SemicircleAt(float Distance)
	{
		return FMath::Sqrt(FMath::Max(0.0f, 1.0f - Distance * Distance));
	}

	// PEAK의 화면을 그대로 복제하지 않고, 긴 이동 경로 위의 이동체가 로딩 진척을 설명하는
	// 핵심 문법만 가져온다. 바다 영역을 넓히고 항로를 화면 중심에 배치해 배가 주인공으로 읽힌다.
	constexpr float VoyageTrackStartX = 460.0f;
	constexpr float VoyageTrackEndX = 1460.0f;
	// 항로를 구체 표면에서 2px만 띄워 선체가 바다 위를 바로 가르는 것처럼 보이게 한다.
	constexpr float VoyageRouteClearance = 2.0f;
	constexpr float VoyageWaveAmplitude = 8.0f;
	constexpr float VoyageSecondaryWaveAmplitude = 2.5f;
	constexpr int32 VoyageSegmentCount = 80;
	// PEAK의 지도 구체처럼 바다가 화면 아래에 놓인 큰 원으로 읽히게 한다.
	constexpr float GlobeCenterX = 960.0f;
	constexpr float GlobeCenterY = 1780.0f;
	constexpr float GlobeRadius = 1100.0f;

	float GlobeVerticalRadiusAt(float X)
	{
		const float DeltaX = X - GlobeCenterX;
		return FMath::Sqrt(FMath::Max(0.0f, GlobeRadius * GlobeRadius - DeltaX * DeltaX));
	}

	/**
	 * 항로 위 한 점(디자인 좌표). Alpha는 트랙 시작 0, 끝 1이다.
	 * 미래 점선·완료 항적·배가 모두 이 하나의 곡선을 공유해야 서로 떨어져 보이지 않는다.
	 * 바다 구체의 윗면을 따라가는 원호에 큰 파형과 잔물결 두 겹을 얹은 것이다.
	 */
	FVector2D VoyageTrackPoint(float Alpha, float Phase)
	{
		const float X = FMath::Lerp(VoyageTrackStartX, VoyageTrackEndX, Alpha);
		return FVector2D(X, GlobeCenterY - GlobeVerticalRadiusAt(X) - VoyageRouteClearance
			+ FMath::Sin(Alpha * 5.0f * PI + Phase) * VoyageWaveAmplitude
			+ FMath::Sin(Alpha * 2.0f * PI - Phase * 0.55f) * VoyageSecondaryWaveAmplitude);
	}

	/** 구체를 가로지르는 스트립의 분할 수. 바다면과 파도 띠가 같은 간격을 써야 한다. */
	constexpr int32 GlobeStripSteps = 112;

	/**
	 * 스트립의 Step번째 세로 조각: X와, 화면 밖으로 잘리고 남은 위/아래 경계.
	 * 파도 띠도 이 경계 안에서만 그려야 사각형 띠가 둥근 구체 밖으로 새지 않는다.
	 */
	struct FGlobeSlice
	{
		explicit FGlobeSlice(int32 Step)
			: X(FMath::Lerp(GlobeCenterX - GlobeRadius, GlobeCenterX + GlobeRadius,
				static_cast<float>(Step) / GlobeStripSteps))
		{
			const float VerticalRadius = GlobeVerticalRadiusAt(X);
			Bottom = FMath::Min(GlobeCenterY + VerticalRadius,
				static_cast<float>(MD_LoadingScreen::DesignSize.Y));
			// 구체 양 끝에서는 윗변이 화면 아래로 내려가므로 폭이 0으로 닫히게 눌러 준다.
			Top = FMath::Min(GlobeCenterY - VerticalRadius, Bottom);
		}

		float X;
		float Top = 0.0f;
		float Bottom = 0.0f;
	};

	// OnPaint의 층 배치. DrawVoyageProgress가 자기 기준으로 VoyageLayerSpan개를 쓰므로 텍스트는
	// 그 위에서 시작해야 하고, OnPaint는 실제로 사용한 마지막 층을 그대로 돌려줘야 한다.
	// 돌려준 값보다 위에 그리면 뒤이어 그려질 형제 위젯이 이 위젯에 가려진다.
	constexpr int32 SkyLayer = 0;
	constexpr int32 CloudLayer = 1;
	constexpr int32 SwellLayer = 2;
	constexpr int32 VoyageLayer = 3;
	constexpr int32 VoyageLayerSpan = 5;
	constexpr int32 TextLayer = VoyageLayer + VoyageLayerSpan;

	// 인트로 두 단계. 배경이 먼저 화면을 덮고 그 위로 문구가 뒤따라 나타난다.
	constexpr float BackgroundFadeEndSeconds = 0.62f;
	constexpr float TextFadeStartSeconds = 0.56f;
	constexpr float TextFadeEndSeconds = 0.84f;
	// 완료 교차 전환. 진행 문구가 빠지는 사이 완료 문구가 겹쳐 올라오고 안착하며 한 번 부푼다.
	constexpr float CompletionExitEndSeconds = 0.18f;
	constexpr float CompletionEnterStartSeconds = 0.12f;
	constexpr float CompletionEnterEndSeconds = 0.38f;
	constexpr float CompletionPulseSeconds = 0.48f;
	constexpr float CompletionPulseScale = 0.10f;

	// 두 연출은 서브시스템이 정한 단계 길이 안에서 끝나야 한다. 문구가 다 나타나기 전에
	// 트래블이 시작되면 블로킹 첫 프레임에 문구가 흐릿하고, 완료 연출이 표시 시간을 넘기면
	// 안착하기도 전에 outro가 걷어간다.
	static_assert(TextFadeEndSeconds <= MD_LoadingScreen::IntroDurationSeconds,
		"인트로가 끝나기 전에 문구가 완전히 나타나 있어야 한다");
	static_assert(CompletionEnterEndSeconds <= MD_LoadingScreen::CompletionDisplaySeconds
		&& CompletionPulseSeconds <= MD_LoadingScreen::CompletionDisplaySeconds,
		"완료 연출은 COMPLETE 표시 시간 안에 끝나야 한다");

	// 진행/완료 문구는 같은 자리에서 교차 전환되므로 앵커와 제목 색을 한 벌만 둔다.
	constexpr float TitleAnchorY = 0.78f;
	constexpr float SubtitleAnchorY = 0.845f;
	const FLinearColor TitleColor(0.98f, 1.0f, 1.0f);

	// 상단의 옅은 하늘색이 수평선의 복숭아빛으로 이어지고, 바다는 맑은 청색으로 식도록 해
	// 일출 직후의 밝기를 만든다. 팔레트는 알파 없이 정의하고 WithAlpha로 페이드만 얹는다.
	const FLinearColor SkyTopColor(0.470f, 0.710f, 0.850f);
	const FLinearColor SkyMidColor(0.690f, 0.810f, 0.850f);
	const FLinearColor SkyGlowColor(0.950f, 0.770f, 0.590f);
	const FLinearColor SkyHorizonColor(1.000f, 0.590f, 0.380f);
	const FLinearColor SeaHorizonColor(0.430f, 0.620f, 0.740f);
	const FLinearColor SeaNearColor(0.290f, 0.570f, 0.710f);
	const FLinearColor SeaDeepColor(0.115f, 0.365f, 0.560f);
	const FLinearColor HorizonLineColor(0.310f, 0.430f, 0.570f);
	const FLinearColor CloudColor(0.955f, 0.925f, 0.865f);
	const FLinearColor CloudShadowColor(0.720f, 0.810f, 0.825f);

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
		{  280.0f, 224.0f, 175.0f, 62.0f, 0.52f, 0.35f,
			{-0.48f, -0.02f, 0.46f}, {0.30f, 0.40f, 0.34f}, {0.62f, 1.00f, 0.72f} },
		{  770.0f, 160.0f, 205.0f, 76.0f, 0.46f, 0.28f,
			{-0.42f, 0.04f, 0.52f}, {0.34f, 0.46f, 0.30f}, {0.68f, 1.00f, 0.58f} },
		{ 1290.0f, 246.0f, 190.0f, 68.0f, 0.50f, 0.42f,
			{-0.48f, -0.04f, 0.46f}, {0.30f, 0.44f, 0.34f}, {0.58f, 1.00f, 0.76f} },
		{ 1740.0f, 176.0f, 145.0f, 52.0f, 0.48f, 0.33f,
			{-0.46f, 0.02f, 0.50f}, {0.32f, 0.42f, 0.30f}, {0.72f, 1.00f, 0.64f} },
	};

	/** 구름 윤곽. U는 -1~1이며, 겹친 반원형 덩어리로 둥근 솜사탕 실루엣을 만든다. */
	float CloudProfileAt(float U, const FCloudBank& Cloud)
	{
		// 낮은 반원형 바닥을 깔아 로브 사이가 끊기지 않고 둥근 한 덩어리로 읽히게 한다.
		float Profile = 0.30f * SemicircleAt(U);
		for (int32 Lump = 0; Lump < 3; ++Lump)
		{
			Profile = FMath::Max(Profile, Cloud.LumpAmp[Lump]
				* SemicircleAt((U - Cloud.LumpOffset[Lump]) / Cloud.LumpWidth[Lump]));
		}
		// 양 끝에도 작은 반원형 로브를 추가해 참고 이미지처럼 둥근 덩어리가 이어진다.
		for (const float SideOffset : {-0.74f, 0.74f})
		{
			Profile = FMath::Max(Profile, 0.64f * SemicircleAt((U - SideOffset) / 0.30f));
		}
		return Profile;
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
	// ImageSize는 두지 않는다. 두 브러시 모두 그릴 때 PaintGeometry로 크기를 직접 넘기므로
	// 브러시의 크기 값은 쓰이지 않고, 텍스처를 교체하면 오히려 어긋난 값으로 남는다.
	ShipBrush.SetResourceObject(InArgs._ShipTexture);
	ShipBrush.DrawAs = ESlateBrushDrawType::Image;
	BeaconBrush.SetResourceObject(InArgs._BeaconTexture);
	BeaconBrush.DrawAs = ESlateBrushDrawType::Image;

	TitleFont = InArgs._Font
		? FSlateFontInfo(InArgs._Font, 34)
		: FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 34);
	// 현재 설정된 ChironGoRoundTC-Black_Font 에셋 자체가 900 굵기다.
	TitleFont.LetterSpacing = 170;
	SubtitleFont = InArgs._Font
		? FSlateFontInfo(InArgs._Font, 15)
		: FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15);
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
	// 인트로는 두 단계다. 0.00~0.62초에 배경이 차오르고, 0.56~0.84초에
	// 텍스트가 뒤이어 등장한다. 배경이 먼저 덮여야 트래블 순간이 가려진다.
	const float Time = static_cast<float>(VisualState->GetElapsedSeconds());
	const float OverallOpacity = VisualState->GetOverallOpacity();
	const float BackgroundOpacity = SmoothRange(Time, 0.0f, BackgroundFadeEndSeconds) * OverallOpacity;
	const float TextOpacity = SmoothRange(Time, TextFadeStartSeconds, TextFadeEndSeconds) * OverallOpacity;

	// 배경은 하늘/수평선/바다 그라데이션 위에 구름 띠와 수평선 아래 너울만 얹는다.
	// 항로와 배는 배경 수평선보다 조금 아래에 두어 바다 위를 이동하는 모습으로 읽히게 한다.
	const float Progress = VisualState->GetVisualProgress();
	const float BackgroundDrift = Progress * BackgroundDriftSpan;
	DrawSkyAndSea(Space, OutDrawElements, LayerId + SkyLayer, BackgroundOpacity);
	DrawCloudBands(Space, OutDrawElements, LayerId + CloudLayer, BackgroundDrift + Time * 6.0f,
		BackgroundOpacity);
	DrawSeaSwell(Space, OutDrawElements, LayerId + SwellLayer, BackgroundDrift * 1.6f + Time * 18.0f,
		BackgroundOpacity);

	// PEAK의 이동형 로딩 인디케이터를 이 프로젝트의 바다/작업선 모티프로 번역한다.
	// 트랙은 항상 길게 남고, 밝아진 항로 끝을 배가 따라가므로 진행 방향을 즉시 읽을 수 있다.
	DrawVoyageProgress(Space, OutDrawElements, LayerId + VoyageLayer, Time, Progress, TextOpacity);

	// 완료 연출은 교차 전환이다. 기존 문구가 위로 빠지고(LoadingExit) 완료 문구가
	// 아래에서 올라와 안착한다(Entrance). Pulse는 안착 순간의 scale 보정.
	const float CompletionElapsed = VisualState->GetCompletionElapsedSeconds();
	const float LoadingExit = SmoothRange(CompletionElapsed, 0.0f, CompletionExitEndSeconds);
	const float Entrance = SmoothRange(CompletionElapsed, CompletionEnterStartSeconds, CompletionEnterEndSeconds);
	const float Pulse = FMath::Sin(FMath::Clamp(CompletionElapsed / CompletionPulseSeconds, 0.0f, 1.0f) * PI)
		* CompletionPulseScale;

	const FTextLayer TextLayers[] =
	{
		{LOCTEXT("LoadingTitle", "LOADING"), TitleFont,
			WithAlpha(TitleColor, TextOpacity * (1.0f - LoadingExit)), TitleAnchorY, 1.0f, -18.0f * LoadingExit},
		{VisualState->Subtitle, SubtitleFont,
			FLinearColor(0.58f, 0.76f, 0.82f, 0.98f * TextOpacity * (1.0f - LoadingExit)),
			SubtitleAnchorY, 1.0f, -12.0f * LoadingExit},
		{LOCTEXT("CompleteTitle", "ARRIVED"), TitleFont,
			WithAlpha(TitleColor, TextOpacity * Entrance), TitleAnchorY, 0.86f + Entrance * 0.14f + Pulse,
			18.0f * (1.0f - Entrance)},
		{VisualState->CompletionSubtitle, SubtitleFont,
			FLinearColor(0.64f, 0.78f, 0.82f, 0.98f * TextOpacity * Entrance),
			SubtitleAnchorY, 1.0f, 12.0f * (1.0f - Entrance)},
	};
	for (const FTextLayer& Layer : TextLayers)
	{
		DrawCenteredText(Space, OutDrawElements, LayerId + TextLayer, Layer);
	}
	DrawCompletionCheck(Space, OutDrawElements, LayerId + TextLayer, Entrance, TextOpacity);

	return LayerId + TextLayer;
}

// 하늘은 화면 전체를 덮고, 바다는 PEAK의 지도 구체처럼 아래쪽의 큰 원으로 그린다.
// 원의 윗변이 둥근 수평선 역할을 하므로 직선 경계보다 레퍼런스의 실루엣이 즉시 읽힌다.
void SMD_LoadingScreenWidget::DrawSkyAndSea(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	const FVector2D LocalSize = Space.Geometry.GetLocalSize();
	const TPair<float, FLinearColor> SkyBands[] =
	{
		{0.0f, SkyTopColor},
		{LocalSize.Y * 0.34f, SkyMidColor},
		{LocalSize.Y * 0.72f, SkyGlowColor},
		{static_cast<float>(LocalSize.Y), SkyHorizonColor},
	};

	FSlateShapeStrip SkyStrip(Space.Geometry);
	for (const TPair<float, FLinearColor>& Band : SkyBands)
	{
		SkyStrip.Add(FVector2D(0.0f, Band.Key), WithAlpha(Band.Value, Opacity));
		SkyStrip.Add(FVector2D(LocalSize.X, Band.Key), WithAlpha(Band.Value, Opacity));
	}
	SkyStrip.Draw(DrawElements, LayerId);

	FSlateShapeStrip GlobeStrip(Space.Geometry);
	TArray<FVector2d> GlobeRim;
	GlobeRim.Reserve(GlobeStripSteps + 1);
	for (int32 Step = 0; Step <= GlobeStripSteps; ++Step)
	{
		const FGlobeSlice Slice(Step);
		const FVector2D Top = Space.Point(FVector2D(Slice.X, Slice.Top));
		GlobeStrip.Add(Top, WithAlpha(SeaHorizonColor, Opacity));
		GlobeStrip.Add(Space.Point(FVector2D(Slice.X, Slice.Bottom)), WithAlpha(SeaDeepColor, Opacity));
		GlobeRim.Add(FVector2d(Top));
	}
	GlobeStrip.Draw(DrawElements, LayerId);
	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), GlobeRim,
		ESlateDrawEffect::None, WithAlpha(HorizonLineColor, 0.72f * Opacity), true,
		FMath::Max(1.0f, 2.0f * Space.Scale));
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
		const FLinearColor TopColor = WithAlpha(CloudColor, Cloud.Alpha * Opacity);
		const FLinearColor BottomColor = WithAlpha(CloudShadowColor, Cloud.Alpha * 0.72f * Opacity);

		FSlateShapeStrip Strip(Space.Geometry);
		for (int32 Step = 0; Step <= ProfileSteps; ++Step)
		{
			const float U = -1.0f + 2.0f * Step / ProfileSteps;
			const float Profile = CloudProfileAt(U, Cloud);
			const float EdgeWeight = FMath::Max(0.0f, 1.0f - FMath::Pow(FMath::Abs(U), 8.0f));
			const float X = CentreX + U * Cloud.HalfWidth;
			Strip.Add(Space.Point(FVector2D(X, Cloud.Y - Cloud.Height * Profile)), TopColor);
			// 아랫면도 완전히 평평하게 두지 않고 둥근 배불뚝이 곡선을 만들어 솜구름처럼 보이게 한다.
			const float BottomBulge = 0.08f + 0.18f * FMath::Sqrt(FMath::Max(0.0f, 1.0f - U * U));
			Strip.Add(Space.Point(FVector2D(X, Cloud.Y + Cloud.Height * BottomBulge * EdgeWeight)), BottomColor);
		}
		Strip.Draw(DrawElements, LayerId);
	}
}

// 둥근 바다 안에서만 흐르는 두 겹의 파도 띠. 각 X에서 원의 윗변보다 아래부터 채우므로
// 사각형 텍스처가 구체 밖으로 새지 않고 둥근 외곽을 그대로 유지한다.
void SMD_LoadingScreenWidget::DrawSeaSwell(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Drift, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	struct FSwellBand { float Depth, Amplitude, Frequency, Speed, Alpha; FLinearColor Color; };
	const FSwellBand Bands[] =
	{
		{ 120.0f, 14.0f, 2.6f, 0.46f, 0.18f, SeaNearColor },
		{ 260.0f, 18.0f, 3.2f, 1.00f, 0.36f, SeaDeepColor },
	};

	for (const FSwellBand& Band : Bands)
	{
		const FLinearColor Color = WithAlpha(Band.Color, Band.Alpha * Opacity);
		FSlateShapeStrip Strip(Space.Geometry);
		for (int32 Step = 0; Step <= GlobeStripSteps; ++Step)
		{
			const FGlobeSlice Slice(Step);
			const float Phase = (Slice.X + Drift * Band.Speed) / GlobeRadius * Band.Frequency * PI;
			// 수평 띠가 구체와 따로 놀지 않도록 각 파도도 외곽선에서 일정 깊이만큼 내려간
			// 원호를 기준으로 삼는다. 작은 사인파는 그 원호 위의 잔물결만 담당한다.
			const float WaveY = Slice.Top + Band.Depth + FMath::Sin(Phase) * Band.Amplitude;
			Strip.Add(Space.Point(FVector2D(Slice.X, FMath::Clamp(WaveY, Slice.Top, Slice.Bottom))), Color);
			Strip.Add(Space.Point(FVector2D(Slice.X, Slice.Bottom)), Color);
		}
		Strip.Draw(DrawElements, LayerId);
	}
}

void SMD_LoadingScreenWidget::DrawVoyageProgress(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Time, float Progress, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	// LayerId 기준: +0 미래 항로, +1 항적 글로우, +2 항적, +3 배, +4 목적지 등대.
	static_assert(VoyageLayerSpan == 5, "여기서 쓰는 층 수와 VoyageLayerSpan이 어긋났다");

	const float WavePhase = Time * 0.72f;
	// 항로 곡선 위의 Alpha(0~1) 지점을 실제 위젯 좌표로 옮긴다.
	const auto TrackPointAt = [&Space, WavePhase](float Alpha)
	{
		return Space.Point(VoyageTrackPoint(Alpha, WavePhase));
	};

	const int32 CompletedSegments =
		FMath::Clamp(FMath::FloorToInt(Progress * VoyageSegmentCount), 0, VoyageSegmentCount);

	TArray<FVector2d> CompletedPoints;
	CompletedPoints.Reserve(CompletedSegments + 2);
	for (int32 Index = 0; Index <= CompletedSegments; ++Index)
	{
		CompletedPoints.Add(FVector2d(TrackPointAt(static_cast<float>(Index) / VoyageSegmentCount)));
	}
	// 세그먼트 사이에 걸친 진행분까지 항적을 이어 붙여야 배가 항적 끝에서 떨어져 보이지 않는다.
	// 마지막 점은 배를 놓는 자리와 같은 TrackPointAt(Progress)이므로 정확히 선수에서 끝난다.
	const float SegmentRemainder = Progress * VoyageSegmentCount - CompletedSegments;
	if (CompletedSegments < VoyageSegmentCount && SegmentRemainder > KINDA_SMALL_NUMBER)
	{
		CompletedPoints.Add(FVector2d(TrackPointAt(Progress)));
	}

	const FLinearColor RouteColor(0.78f, 0.95f, 0.98f, 0.72f * Opacity);
	const FLinearColor WakeGlow(0.88f, 0.96f, 1.0f, 0.18f * Opacity);
	const FLinearColor WakeColor(1.0f, 1.0f, 1.0f, 0.96f * Opacity);

	// 완료/미완료 경로 모두 같은 TrackPointAt을 사용하고 같은 중심 두께로 그린다. 미래 점선은
	// 현재 진행점 앞에서만 시작시켜 굵은 항적 아래에 겹치거나 배의 앞뒤 높이가 달라 보이지 않게 한다.
	// 끊긴 선분은 MakeLines를 여러 번 부르는 수밖에 없으므로(점을 이어 버린다), 대신 점 버퍼
	// 하나를 돌려 써서 프레임마다 스무 번씩 배열을 새로 잡지 않게 한다.
	TArray<FVector2d> Dash;
	Dash.SetNumUninitialized(2);
	for (int32 Index = 0; Index < VoyageSegmentCount; Index += 4)
	{
		const int32 EndIndex = FMath::Min(Index + 2, VoyageSegmentCount);
		const float DashStartAlpha = FMath::Max(Progress, static_cast<float>(Index) / VoyageSegmentCount);
		const float DashEndAlpha = static_cast<float>(EndIndex) / VoyageSegmentCount;
		if (DashEndAlpha <= DashStartAlpha) { continue; }
		Dash[0] = FVector2d(TrackPointAt(DashStartAlpha));
		Dash[1] = FVector2d(TrackPointAt(DashEndAlpha));
		FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), Dash,
			ESlateDrawEffect::None, RouteColor, true, 4.0f * Space.Scale);
	}
	if (CompletedPoints.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 1, Space.Geometry.ToPaintGeometry(), CompletedPoints,
			ESlateDrawEffect::None, WakeGlow, true, 9.0f * Space.Scale);
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 2, Space.Geometry.ToPaintGeometry(), CompletedPoints,
			ESlateDrawEffect::None, WakeColor, true, 4.0f * Space.Scale);
	}

	// 목적지는 중간 표식이나 광선 없이 투명 PNG 등대 아이콘 하나로 표시한다.
	// 항로 종점 바로 옆, 구체 표면 위에 놓아 배의 도착 지점이 자연스럽게 이어진다.
	if (BeaconBrush.GetResourceObject())
	{
		constexpr float BeaconX = VoyageTrackEndX + 20.0f;
		const float BeaconSurfaceY = GlobeCenterY - GlobeVerticalRadiusAt(BeaconX);
		const FVector2D BeaconSize(112.0f * Space.Scale, 112.0f * Space.Scale);
		const FVector2D BeaconPosition = Space.Point(FVector2D(BeaconX, BeaconSurfaceY - 58.0f));
		FSlateDrawElement::MakeBox(DrawElements, LayerId + 4,
			Space.Geometry.ToPaintGeometry(BeaconSize, FSlateLayoutTransform(BeaconPosition - BeaconSize * 0.5f),
				FSlateRenderTransform(), FVector2D(0.5f, 0.98f)),
			&BeaconBrush, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
	}

	// 텍스처가 지정되지 않았으면 트랙만 남기고 배는 생략한다.
	if (!ShipBrush.GetResourceObject()) { return; }

	// 좌우 반전된 배의 뱃머리 쪽 바닥을 항로에 고정한다. 항로 종점과 등대가 같은 X축이어도
	// 완료 시 선체 전체가 등대 위를 덮지 않고, 선수만 목적지에 닿는 모습이 된다.
	const float Bob = FMath::Sin(Time * 2.4f) * 1.5f;
	const FVector2D ShipSize(230.0f * Space.Scale, 72.0f * Space.Scale);
	const FVector2D ShipBottom = TrackPointAt(Progress) + FVector2D(0.0f, Bob) * Space.Scale;
	const FVector2D ShipPivot(0.12f, 0.86f);
	const FVector2D ShipPosition = ShipBottom - ShipSize * ShipPivot;
	// 짧은 파형 노이즈보다 전체 원호의 방향을 읽도록 양옆 두 샘플의 간격을 확보한다.
	constexpr float TangentSample = 0.02f;
	const FVector2D TangentStart = TrackPointAt(FMath::Max(0.0f, Progress - TangentSample));
	const FVector2D TangentEnd = TrackPointAt(FMath::Min(1.0f, Progress + TangentSample));
	const FVector2D Tangent = TangentEnd - TangentStart;
	const float ShipAngle = FMath::Clamp(FMath::Atan2(Tangent.Y, Tangent.X),
		FMath::DegreesToRadians(-24.0f), FMath::DegreesToRadians(24.0f));

	// T_Ship 원본은 메인 메뉴용이라 뱃머리가 왼쪽을 본다. 항로는 왼→오른쪽으로 나아가므로
	// 좌우 반전한 뒤 항로 접선만큼 회전한다.
	const FSlateRenderTransform ShipFacingRoute(::Concatenate(
		FScale2D(-1.0f, 1.0f), FQuat2D(ShipAngle)));
	FSlateDrawElement::MakeBox(DrawElements, LayerId + 3,
		Space.Geometry.ToPaintGeometry(ShipSize, FSlateLayoutTransform(ShipPosition), ShipFacingRoute, ShipPivot),
		&ShipBrush, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
}

void SMD_LoadingScreenWidget::DrawCompletionCheck(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Progress, float Opacity) const
{
	if (Progress <= KINDA_SMALL_NUMBER || Opacity <= KINDA_SMALL_NUMBER) { return; }

	const FVector2D Center = Space.Point(
		FVector2D(MD_LoadingScreen::DesignSize.X * 0.5f, MD_LoadingScreen::DesignSize.Y * 0.705f));
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
