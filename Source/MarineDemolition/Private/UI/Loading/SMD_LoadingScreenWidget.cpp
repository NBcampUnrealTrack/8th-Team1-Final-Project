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
	constexpr int32 VoyageSegmentCount = 80;
	// PEAK의 지도 구체처럼 바다가 화면 아래에 놓인 큰 원으로 읽히게 한다.
	constexpr float GlobeCenterX = 960.0f;
	constexpr float GlobeCenterY = 1780.0f;
	constexpr float GlobeHorizontalRadius = 1260.0f;
	constexpr float GlobeVerticalRadius = 1100.0f;
	// 항로는 구체 중심 위에서 가장 높고 양옆으로 처지는 얕은 포물선이다. 계수가 클수록 더 굽는다.
	constexpr float VoyageApexY = 634.0f;
	constexpr float VoyageArcCurvature = 0.00020f;

	float GlobeVerticalRadiusAt(float X)
	{
		const float NormalizedX = (X - GlobeCenterX) / GlobeHorizontalRadius;
		return GlobeVerticalRadius
			* FMath::Sqrt(FMath::Max(0.0f, 1.0f - NormalizedX * NormalizedX));
	}

	/** 아래에서 올라온 뒤 제자리 위를 살짝 넘었다가 안착하는 인트로 오프셋. */
	float IntroRiseOffset(float Time, float Start, float End, float Distance, float Overshoot)
	{
		const float Alpha = SmoothRange(Time, Start, End);
		const float Rise = 1.0f - SmoothRange(Alpha, 0.0f, 0.78f);
		const float Settle = SmoothRange(Alpha, 0.78f, 1.0f);
		return Rise * Distance - FMath::Sin(Settle * PI) * Overshoot;
	}

	/**
	 * 항로 위 한 점(디자인 좌표). Alpha는 트랙 시작 0, 끝 1이다.
	 * 미래 항로·완료 항적·배·등대가 모두 이 하나의 곡선을 공유해야 서로 떨어져 보이지 않는다.
	 * 바다 구체 외곽과 맞춘 고정 곡선이며, 움직임은 배 주변에만 둔다.
	 */
	FVector2D VoyageTrackPoint(float Alpha)
	{
		const float X = FMath::Lerp(VoyageTrackStartX, VoyageTrackEndX, Alpha);
		const float FromCenter = X - GlobeCenterX;
		return FVector2D(X, VoyageApexY + VoyageArcCurvature * FromCenter * FromCenter);
	}

	/** 구체를 가로지르는 스트립의 분할 수. 바다면과 파도 띠가 같은 간격을 써야 한다. */
	constexpr int32 GlobeStripSteps = 112;
	/** 파도 띠가 흘러가는 속도(경과 시간 배율). */
	constexpr float SwellDriftRate = 4.0f;
	// 비정수 화면 배율과 인트로 안착 오버슈트에서도 마지막 래스터 행이 비지 않도록
	// 구체 자체를 화면 아래로 충분히 연장한다.
	constexpr float GlobeBottomOverscan = 12.0f;

	/**
	 * 스트립의 Step번째 세로 조각: X와, 화면 밖으로 잘리고 남은 위/아래 경계.
	 * 파도 띠도 이 경계 안에서만 그려야 사각형 띠가 둥근 구체 밖으로 새지 않는다.
	 */
	struct FGlobeSlice
	{
		explicit FGlobeSlice(int32 Step)
			: X(FMath::Lerp(GlobeCenterX - GlobeHorizontalRadius, GlobeCenterX + GlobeHorizontalRadius,
				static_cast<float>(Step) / GlobeStripSteps))
		{
			const float VerticalRadius = GlobeVerticalRadiusAt(X);
			Bottom = FMath::Min(GlobeCenterY + VerticalRadius,
				static_cast<float>(MD_LoadingScreen::DesignSize.Y) + GlobeBottomOverscan);
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
	constexpr float BackgroundFadeEndSeconds = 0.50f;
	constexpr float VoyageFadeStartSeconds = 0.10f;
	constexpr float VoyageFadeEndSeconds = 0.42f;
	constexpr float TextFadeStartSeconds = 0.56f;
	constexpr float TextFadeEndSeconds = 0.84f;
	constexpr float GlobeRiseEndSeconds = 0.82f;
	constexpr float CloudRiseStartSeconds = 0.08f;
	constexpr float CloudRiseEndSeconds = 0.86f;
	constexpr float VoyageRiseStartSeconds = 0.14f;
	constexpr float VoyageRiseEndSeconds = 0.88f;
	// 완료 교차 전환. 진행 문구가 빠지는 사이 완료 문구가 겹쳐 올라오고 안착하며 한 번 부푼다.
	constexpr float CompletionExitEndSeconds = 0.18f;
	constexpr float CompletionEnterStartSeconds = 0.12f;
	constexpr float CompletionEnterEndSeconds = 0.38f;
	constexpr float CompletionPulseSeconds = 0.48f;
	constexpr float CompletionPulseScale = 0.10f;
	// 도착 순간 등대에서 한 번 번쩍이는 빛. 끝나는 시각은 시작+길이로 계산해 둘이 어긋나지 않게 한다.
	constexpr float BeaconFlashStartSeconds = 0.38f;
	constexpr float BeaconFlashDurationSeconds = 0.40f;

	static_assert(BeaconFlashStartSeconds + BeaconFlashDurationSeconds
		<= MD_LoadingScreen::CompletionDisplaySeconds,
		"등대 섬광은 COMPLETE 표시 시간 안에 끝나야 한다");

	// 두 연출은 서브시스템이 정한 단계 길이 안에서 끝나야 한다. 문구가 다 나타나기 전에
	// 트래블이 시작되면 블로킹 첫 프레임에 문구가 흐릿하고, 완료 연출이 표시 시간을 넘기면
	// 안착하기도 전에 outro가 걷어간다.
	static_assert(TextFadeEndSeconds <= MD_LoadingScreen::IntroDurationSeconds
		&& VoyageRiseEndSeconds <= MD_LoadingScreen::IntroDurationSeconds,
		"인트로가 끝나기 전에 문구가 완전히 나타나 있어야 한다");
	static_assert(CompletionEnterEndSeconds <= MD_LoadingScreen::CompletionDisplaySeconds
		&& CompletionPulseSeconds <= MD_LoadingScreen::CompletionDisplaySeconds,
		"완료 연출은 COMPLETE 표시 시간 안에 끝나야 한다");

	// 진행/완료 문구는 같은 자리에서 교차 전환되므로 앵커와 색을 한 벌만 둔다.
	constexpr float TitleAnchorY = 0.425f;
	constexpr float SubtitleAnchorY = 0.80f;
	constexpr float SubtitleOpacity = 0.98f;
	const FLinearColor TitleColor(1.0f, 0.92f, 0.72f);
	const FLinearColor SubtitleColor(0.58f, 0.76f, 0.82f);

	// 상단의 옅은 하늘색이 수평선의 복숭아빛으로 이어지고, 바다는 맑은 청색으로 식도록 해
	// 일출 직후의 밝기를 만든다. 팔레트는 알파 없이 정의하고 WithAlpha로 페이드만 얹는다.
	const FLinearColor SkyTopColor(0.470f, 0.710f, 0.850f);
	const FLinearColor SkyMidColor(0.690f, 0.810f, 0.850f);
	const FLinearColor SkyGlowColor(0.950f, 0.770f, 0.590f);
	const FLinearColor SkyHorizonColor(0.970f, 0.730f, 0.560f);
	const FLinearColor SeaHorizonColor(0.430f, 0.620f, 0.740f);
	const FLinearColor SeaNearColor(0.290f, 0.570f, 0.710f);
	const FLinearColor SeaDeepColor(0.115f, 0.365f, 0.560f);
	const FLinearColor HorizonLineColor(0.310f, 0.430f, 0.570f);
	// 레퍼런스의 종이 오려 붙인 듯한 2단 구름: 옅은 청백색 몸통 위에 따뜻한 흰색 캡을
	// 불투명하게 얹는다. 반투명 그라데이션보다 하늘에서 윤곽과 내부 굴곡이 또렷하다.
	const FLinearColor CloudBodyColor(0.690f, 0.820f, 0.885f);
	const FLinearColor CloudCapColor(1.000f, 0.990f, 0.965f);

	/** 구름 윤곽의 분할 수. 타원 경계가 저해상도에서도 각져 보이지 않을 만큼 잘게 나눈다. */
	constexpr int32 CloudProfileSteps = 128;

	/** 각 구름을 이루는 타원 로브. 모든 값은 해당 구름의 HalfWidth/Height 기준 비율이다. */
	struct FCloudLobe
	{
		float X, Y, RadiusX, RadiusY;
	};

	/**
	 * 구름 한 덩어리. 로브마다 중심 높이와 두 축 반경이 달라 납작한 동일 프로필이 반복되지 않는다.
	 * 첫 로브는 전체를 잇는 넓은 몸통이고, 나머지는 레퍼런스처럼 위아래로 포개진 솜 봉우리다.
	 */
	struct FCloudBank
	{
		float X, Y, HalfWidth, Height, Alpha, DetailAlpha;
		int32 LobeCount;
		FCloudLobe Lobes[10];
	};
	const FCloudBank CloudBanks[] =
	{
		// 넓은 전경 구름, 작고 먼 조각구름, 높은 덩어리, 낮게 뒤따르는 구름 순.
		// 아래쪽 로브가 예전의 납작한 타원 밑면을 흩뜨려 같은 모양이 반복되지 않는다.
		{ 260.f, 265.f, 210.f, 130.f, 1.f, 1.f, 9,
			{{0.f,.16f,.85f,.23f}, {-.65f,.03f,.37f,.42f},
			 {-.32f,-.22f,.42f,.64f}, {.18f,-.10f,.48f,.52f},
			 {.62f,.05f,.34f,.35f}, {.85f,.17f,.23f,.23f},
			 {-.5f,.28f,.30f,.30f}, {-.05f,.30f,.40f,.34f}, {.45f,.30f,.34f,.30f}} },
		{ 820.f, 150.f, 148.f, 80.f, .84f, .70f, 7,
			{{0.f,.16f,.83f,.22f}, {-.65f,.07f,.38f,.35f},
			 {-.30f,-.10f,.43f,.48f}, {.21f,-.22f,.44f,.60f},
			 {.66f,.10f,.39f,.31f}, {-.30f,.30f,.39f,.29f}, {.36f,.29f,.42f,.28f}} },
		{ 1490.f, 260.f, 265.f, 165.f, 1.f, 1.f, 10,
			{{0.f,.16f,.88f,.23f}, {-.76f,.11f,.30f,.33f},
			 {-.49f,-.10f,.38f,.49f}, {-.14f,-.37f,.42f,.72f},
			 {.25f,-.13f,.39f,.54f}, {.58f,.05f,.36f,.39f},
			 {.85f,.20f,.25f,.23f}, {-.52f,.29f,.33f,.32f},
			 {-.02f,.32f,.40f,.32f}, {.47f,.30f,.36f,.31f}} },
		{ 790.f, 426.f, 172.f, 67.f, .88f, .78f, 7,
			{{0.f,.14f,.87f,.23f}, {-.70f,.08f,.35f,.34f},
			 {-.35f,-.18f,.39f,.52f}, {.13f,-.08f,.44f,.44f},
			 {.63f,.08f,.40f,.34f}, {-.32f,.27f,.37f,.29f}, {.31f,.29f,.41f,.27f}} },
	};

	/** 지정 X에서 모든 타원 로브의 합집합 위/아래 경계를 구한다. */
	void CloudEnvelopeAt(float U, const FCloudBank& Cloud, float& OutTop, float& OutBottom)
	{
		OutTop = BIG_NUMBER;
		OutBottom = -BIG_NUMBER;
		for (int32 Index = 0; Index < Cloud.LobeCount; ++Index)
		{
			const FCloudLobe& Lobe = Cloud.Lobes[Index];
			const float LocalX = (U - Lobe.X) / Lobe.RadiusX;
			if (FMath::Abs(LocalX) > 1.0f) { continue; }
			const float Vertical = Lobe.RadiusY * SemicircleAt(LocalX);
			OutTop = FMath::Min(OutTop, Lobe.Y - Vertical);
			OutBottom = FMath::Max(OutBottom, Lobe.Y + Vertical);
		}
		// 첫 번째 몸통 로브가 전 구간을 덮으므로 이 fallback은 부동소수점 끝점 오차만 막는다.
		if (OutTop == BIG_NUMBER) { OutTop = OutBottom = Cloud.Lobes[0].Y; }
	}

	/**
	 * Slate에는 채워진 다각형을 그리는 기본 요소가 없다. 두꺼운 선을 여러 개 이어 붙여 채우면
	 * 알파가 겹치는 자리마다 이음매가 보이므로, 정점을 직접 만들어 삼각형 스트립으로 넘긴다.
	 * 위/아래 정점을 번갈아 넣고 마지막에 Draw를 부르면 된다.
	 */
	struct FSlateShapeStrip
	{
		explicit FSlateShapeStrip(const FGeometry& InGeometry)
			: RenderTransform(InGeometry.ToPaintGeometry().GetAccumulatedRenderTransform())
		{
			// 가장 촘촘한 스트립(구름 윤곽)이 한 번에 다 들어가도록 잡아 재할당을 없앤다.
			Vertices.Reserve(2 * (FMath::Max(CloudProfileSteps, GlobeStripSteps) + 1));
		}

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

	void DrawCloudShape(const FMD_LoadingDesignSpace& Space, FSlateWindowElementList& DrawElements,
		int32 LayerId, const FCloudBank& Cloud, const FVector2D& Centre, float Opacity)
	{
		// 실제 로브의 좌우 끝을 쓴다. 예전의 고정 [-1,1] 범위는 양 끝 로브를 세로로 잘라냈다.
		float MinU = BIG_NUMBER, MaxU = -BIG_NUMBER;
		for (int32 Index = 0; Index < Cloud.LobeCount; ++Index)
		{
			MinU = FMath::Min(MinU, Cloud.Lobes[Index].X - Cloud.Lobes[Index].RadiusX);
			MaxU = FMath::Max(MaxU, Cloud.Lobes[Index].X + Cloud.Lobes[Index].RadiusX);
		}
		const FLinearColor BodyColor = WithAlpha(
			CloudBodyColor, Cloud.Alpha * Cloud.DetailAlpha * Opacity);
		const FLinearColor OuterColor = WithAlpha(CloudCapColor, Cloud.Alpha * Opacity);

		FSlateShapeStrip OuterStrip(Space.Geometry);
		for (int32 Step = 0; Step <= CloudProfileSteps; ++Step)
		{
			const float U = FMath::Lerp(MinU, MaxU, static_cast<float>(Step) / CloudProfileSteps);
			float Top, Bottom;
			CloudEnvelopeAt(U, Cloud, Top, Bottom);
			const float X = Centre.X + U * Cloud.HalfWidth;
			OuterStrip.Add(Space.Point(FVector2D(X, Centre.Y + Cloud.Height * Top)), OuterColor);
			OuterStrip.Add(Space.Point(FVector2D(X, Centre.Y + Cloud.Height * Bottom)), OuterColor);
		}
		OuterStrip.Draw(DrawElements, LayerId);

		// 아래쪽 안에만 작은 솜구름 군집을 넣는다. 위 경계는 크기가 다른 세 반원으로 만들고,
		// 아래 경계는 실제 외곽을 따라가되 흰 여백을 남긴다. 따라서 테두리처럼 전체를 복제한
		// 모양이나 긴 수평 띠가 아니라 레퍼런스의 불규칙한 청백색 음영 덩어리로 읽힌다.
		if (Cloud.DetailAlpha <= KINDA_SMALL_NUMBER) { return; }

		constexpr float AccentHalfSpan = 0.98f;
		FSlateShapeStrip AccentStrip(Space.Geometry);
		for (int32 Step = 0; Step <= CloudProfileSteps; ++Step)
		{
			const float V = -1.0f + 2.0f * Step / CloudProfileSteps;
			const float U = FMath::Lerp(MinU, MaxU, (V * AccentHalfSpan + 1.0f) * 0.5f);
			float OuterTop, OuterBottom;
			CloudEnvelopeAt(U, Cloud, OuterTop, OuterBottom);
			float Puff = 0.10f * SemicircleAt(V);
			Puff = FMath::Max(Puff, 0.27f * SemicircleAt((V + 0.58f) / 0.30f));
			Puff = FMath::Max(Puff, 0.44f * SemicircleAt((V + 0.08f) / 0.39f));
			Puff = FMath::Max(Puff, 0.32f * SemicircleAt((V - 0.50f) / 0.33f));
			const float Edge = FMath::Pow(FMath::Max(0.0f, 1.0f - V * V), 0.65f);
			const float AccentBottom = OuterBottom - 0.009f;
			const float DesiredTop = 0.22f - Puff;
			const float AccentTop = FMath::Lerp(AccentBottom, DesiredTop, Edge);
			const float X = Centre.X + U * Cloud.HalfWidth;
			AccentStrip.Add(Space.Point(FVector2D(
				X, Centre.Y + Cloud.Height * FMath::Max(OuterTop + 0.04f, AccentTop))), BodyColor);
			AccentStrip.Add(Space.Point(FVector2D(
				X, Centre.Y + Cloud.Height * AccentBottom)), BodyColor);
		}
		AccentStrip.Draw(DrawElements, LayerId);
	}
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
	BackgroundBrush.SetResourceObject(InArgs._BackgroundTexture);
	BackgroundBrush.DrawAs = ESlateBrushDrawType::Image;

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
	const float VoyageOpacity = SmoothRange(Time, VoyageFadeStartSeconds, VoyageFadeEndSeconds) * OverallOpacity;
	const float TextOpacity = SmoothRange(Time, TextFadeStartSeconds, TextFadeEndSeconds) * OverallOpacity;
	// 배경을 한 장처럼 페이드하지 않고 깊이별로 조금씩 늦게 떠오르게 해 첫 장면에 입체감을 준다.
	const float GlobeOffsetY = IntroRiseOffset(Time, 0.02f, GlobeRiseEndSeconds, 220.0f, 10.0f);
	const float CloudOffsetY = IntroRiseOffset(
		Time, CloudRiseStartSeconds, CloudRiseEndSeconds, 120.0f, 7.0f);
	const float VoyageOffsetY = IntroRiseOffset(
		Time, VoyageRiseStartSeconds, VoyageRiseEndSeconds, 160.0f, 12.0f);

	// 배경은 하늘/수평선/바다 그라데이션 위에 구름 띠와 수평선 아래 너울만 얹는다.
	// 항로와 배는 배경 수평선보다 조금 아래에 두어 바다 위를 이동하는 모습으로 읽히게 한다.
	const float Progress = VisualState->GetVisualProgress();
	DrawSkyAndSea(Space, OutDrawElements, LayerId + SkyLayer, GlobeOffsetY, BackgroundOpacity);
	if (!BackgroundBrush.GetResourceObject())
	{
		// 배경 일러스트가 지정되지 않았을 때만 도는 절차형 fallback이다.
		DrawCloudBands(Space, OutDrawElements, LayerId + CloudLayer, CloudOffsetY, BackgroundOpacity);
		DrawSeaSwell(Space, OutDrawElements, LayerId + SwellLayer, Time * SwellDriftRate, GlobeOffsetY,
			BackgroundOpacity);
	}

	// PEAK의 이동형 로딩 인디케이터를 이 프로젝트의 바다/작업선 모티프로 번역한다.
	// 트랙은 항상 길게 남고, 밝아진 항로 끝을 배가 따라가므로 진행 방향을 즉시 읽을 수 있다.
	DrawVoyageProgress(Space, OutDrawElements, LayerId + VoyageLayer, Time, Progress,
		VoyageOffsetY, VoyageOpacity);

	// 완료 연출은 교차 전환이다. 기존 문구가 위로 빠지고(LoadingExit) 완료 문구가
	// 아래에서 올라와 안착한다(Entrance). Pulse는 안착 순간의 scale 보정.
	const float CompletionElapsed = VisualState->GetCompletionElapsedSeconds();
	const float LoadingExit = SmoothRange(CompletionElapsed, 0.0f, CompletionExitEndSeconds);
	const float Entrance = SmoothRange(CompletionElapsed, CompletionEnterStartSeconds, CompletionEnterEndSeconds);
	const float Pulse = FMath::Sin(FMath::Clamp(CompletionElapsed / CompletionPulseSeconds, 0.0f, 1.0f) * PI)
		* CompletionPulseScale;

	const FTextLayer TextLayers[] =
	{
		{VisualState->Subtitle, SubtitleFont,
			WithAlpha(SubtitleColor, SubtitleOpacity * TextOpacity * (1.0f - LoadingExit)),
			SubtitleAnchorY, 1.0f, -12.0f * LoadingExit},
		{LOCTEXT("CompleteTitle", "ARRIVED"), TitleFont,
			WithAlpha(TitleColor, TextOpacity * Entrance), TitleAnchorY, 0.86f + Entrance * 0.14f + Pulse,
			18.0f * (1.0f - Entrance)},
		{VisualState->CompletionSubtitle, SubtitleFont,
			WithAlpha(SubtitleColor, SubtitleOpacity * TextOpacity * Entrance),
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
	FSlateWindowElementList& DrawElements, int32 LayerId, float GlobeOffsetY, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	if (BackgroundBrush.GetResourceObject())
	{
		FSlateDrawElement::MakeBox(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(),
			&BackgroundBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, Opacity));
		return;
	}

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
	// 안착 오버슈트에서는 GlobeOffsetY가 음수가 된다. 윗면은 그대로 위로 넘겨 생동감을
	// 유지하되 하단까지 함께 올리면 화면 밑이 열린다. 하단은 항상 viewport 아래에 고정한다.
	const float BottomFillOffsetY = FMath::Max(0.0f, GlobeOffsetY);
	for (int32 Step = 0; Step <= GlobeStripSteps; ++Step)
	{
		const FGlobeSlice Slice(Step);
		const FVector2D Top = Space.Point(FVector2D(Slice.X, Slice.Top + GlobeOffsetY));
		GlobeStrip.Add(Top, WithAlpha(SeaHorizonColor, Opacity));
		GlobeStrip.Add(Space.Point(FVector2D(Slice.X, Slice.Bottom + BottomFillOffsetY)),
			WithAlpha(SeaDeepColor, Opacity));
		GlobeRim.Add(FVector2d(Top));
	}
	GlobeStrip.Draw(DrawElements, LayerId);

	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), GlobeRim,
		ESlateDrawEffect::None, WithAlpha(HorizonLineColor, 0.72f * Opacity), true,
		FMath::Max(1.0f, 2.0f * Space.Scale));
}

// 구름 덩어리. 따뜻한 흰색 외곽 안에 옅은 청백색 음영 덩어리를 겹쳐 2단으로 그린다.
// 위/아래 윤곽이 양 끝에서 같은 높이로 모이므로 덩어리가 저절로 닫혀 잘린 자국이 없다.
void SMD_LoadingScreenWidget::DrawCloudBands(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float OffsetY, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	for (const FCloudBank& Cloud : CloudBanks)
	{
		DrawCloudShape(Space, DrawElements, LayerId, Cloud, FVector2D(Cloud.X, Cloud.Y + OffsetY), Opacity);
	}
}

// 둥근 바다 안에서만 흐르는 두 겹의 파도 띠. 각 X에서 원의 윗변보다 아래부터 채우므로
// 사각형 텍스처가 구체 밖으로 새지 않고 둥근 외곽을 그대로 유지한다.
void SMD_LoadingScreenWidget::DrawSeaSwell(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Drift, float GlobeOffsetY, float Opacity) const
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
		const float BottomFillOffsetY = FMath::Max(0.0f, GlobeOffsetY);
		for (int32 Step = 0; Step <= GlobeStripSteps; ++Step)
		{
			const FGlobeSlice Slice(Step);
			const float Phase = (Slice.X + Drift * Band.Speed) / GlobeHorizontalRadius * Band.Frequency * PI;
			// 수평 띠가 구체와 따로 놀지 않도록 각 파도도 외곽선에서 일정 깊이만큼 내려간
			// 원호를 기준으로 삼는다. 작은 사인파는 그 원호 위의 잔물결만 담당한다.
			const float WaveY = Slice.Top + Band.Depth + FMath::Sin(Phase) * Band.Amplitude;
			Strip.Add(Space.Point(FVector2D(
				Slice.X, FMath::Clamp(WaveY, Slice.Top, Slice.Bottom) + GlobeOffsetY)), Color);
			Strip.Add(Space.Point(FVector2D(Slice.X, Slice.Bottom + BottomFillOffsetY)), Color);
		}
		Strip.Draw(DrawElements, LayerId);
	}
}

void SMD_LoadingScreenWidget::DrawVoyageProgress(const FMD_LoadingDesignSpace& Space,
	FSlateWindowElementList& DrawElements, int32 LayerId, float Time, float Progress,
	float OffsetY, float Opacity) const
{
	if (Opacity <= KINDA_SMALL_NUMBER) { return; }

	// LayerId 기준: +0 미래 항로, +1 선미 물결, +2 항적, +3 배, +4 목적지 등대.
	static_assert(VoyageLayerSpan == 5, "여기서 쓰는 층 수와 VoyageLayerSpan이 어긋났다");

	// 시점은 고정이다. 배만 나아가고 목적지와 배경은 제자리에 있는다.
	// 항로 곡선 위의 Alpha(0~1) 지점을 실제 위젯 좌표로 옮긴다.
	const auto TrackPointAt = [&Space, OffsetY](float Alpha)
	{
		return Space.Point(VoyageTrackPoint(Alpha) + FVector2D(0.0f, OffsetY));
	};

	// 전체 경로를 한 번만 계산한다. 항적은 이 배열의 앞부분을 그대로 다시 쓴다.
	TArray<FVector2d> RoutePoints;
	RoutePoints.Reserve(VoyageSegmentCount + 2);
	for (int32 Index = 0; Index <= VoyageSegmentCount; ++Index)
	{
		RoutePoints.Add(FVector2d(TrackPointAt(static_cast<float>(Index) / VoyageSegmentCount)));
	}

	const FLinearColor RouteColor(0.10f, 0.28f, 0.40f, 0.70f * Opacity);
	const FLinearColor WakeColor(1.0f, 1.0f, 1.0f, Opacity);

	// 전체 경로는 얇고 어두운 선으로 먼저 깔고, 배가 지나간 자리만 흰색으로 덮는다.
	// 진행선의 끝과 배의 기준점은 같은 TrackPointAt을 사용해 한 프레임도 떨어지지 않는다.
	FSlateDrawElement::MakeLines(DrawElements, LayerId, Space.Geometry.ToPaintGeometry(), RoutePoints,
		ESlateDrawEffect::None, RouteColor, true, 3.0f * Space.Scale);

	const int32 CompletedSegments =
		FMath::Clamp(FMath::FloorToInt(Progress * VoyageSegmentCount), 0, VoyageSegmentCount);

	TArray<FVector2d> CompletedPoints(RoutePoints.GetData(), CompletedSegments + 1);
	// 세그먼트 사이에 걸친 진행분까지 항적을 이어 붙여야 배가 항적 끝에서 떨어져 보이지 않는다.
	// 마지막 점은 배를 놓는 자리와 같은 TrackPointAt(Progress)이므로 정확히 선수에서 끝난다.
	const float SegmentRemainder = Progress * VoyageSegmentCount - CompletedSegments;
	if (CompletedSegments < VoyageSegmentCount && SegmentRemainder > KINDA_SMALL_NUMBER)
	{
		CompletedPoints.Add(FVector2d(TrackPointAt(Progress)));
	}
	if (CompletedPoints.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(DrawElements, LayerId + 2, Space.Geometry.ToPaintGeometry(), CompletedPoints,
			ESlateDrawEffect::None, WakeColor, true, 5.0f * Space.Scale);
	}

	const float CompletionTime = VisualState->GetCompletionElapsedSeconds();
	const float Sailing = 1.0f - SmoothRange(CompletionTime, 0.04f, 0.40f);

	// 실제 완료 시에만 등대 꼭대기에서 짧은 빛줄기가 한 번 벌어졌다 사라진다.
	// 별도 원호 계산을 하지 않고 항로 종점을 받침으로 직접 써 카메라 이동이나 화면 비율이
	// 달라져도 등대와 선이 벌어지지 않는다.
	if (BeaconBrush.GetResourceObject())
	{
		const FVector2D BeaconSize(90.0f * Space.Scale, 90.0f * Space.Scale);
		// PNG 아래쪽 투명 여백을 보정해 실제 등대 받침이 항로 끝에 닿도록 한다.
		const FVector2D BeaconBase = TrackPointAt(1.0f)
			+ FVector2D(0.0f, 14.0f * Space.Scale);
		const FVector2D BeaconPosition = BeaconBase - FVector2D(0.0f, BeaconSize.Y * 0.5f);
		FSlateDrawElement::MakeBox(DrawElements, LayerId + 4,
			Space.Geometry.ToPaintGeometry(BeaconSize, FSlateLayoutTransform(BeaconPosition - BeaconSize * 0.5f),
				FSlateRenderTransform(), FVector2D(0.5f, 0.98f)),
			&BeaconBrush, ESlateDrawEffect::None, WithAlpha(TitleColor, Opacity));
		if (CompletionTime > BeaconFlashStartSeconds
			&& CompletionTime < BeaconFlashStartSeconds + BeaconFlashDurationSeconds)
		{
			const float FlashAge = (CompletionTime - BeaconFlashStartSeconds) / BeaconFlashDurationSeconds;
			const float FlashAlpha = FMath::Sin(FlashAge * PI) * Opacity;
			const FVector2D Lamp = BeaconBase - FVector2D(0.0f, 59.0f * Space.Scale);
			TArray<FVector2d> Ray;
			Ray.Reserve(2);
			for (int32 Index = 0; Index < 5; ++Index)
			{
				const float Angle = FMath::DegreesToRadians(200.0f + Index * 40.0f);
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				Ray.Reset();
				Ray.Add(FVector2d(Lamp + Direction * (13.0f + FlashAge * 4.0f) * Space.Scale));
				Ray.Add(FVector2d(Lamp + Direction * (24.0f + FlashAge * 16.0f) * Space.Scale));
				FSlateDrawElement::MakeLines(DrawElements, LayerId + 4, Space.Geometry.ToPaintGeometry(), Ray,
					ESlateDrawEffect::None, WithAlpha(TitleColor, FlashAlpha), true, 2.5f * Space.Scale);
			}
		}
	}

	// 텍스처가 지정되지 않았으면 트랙만 남기고 배는 생략한다.
	if (!ShipBrush.GetResourceObject()) { return; }

	// 좌우 반전된 배의 뱃머리 쪽 바닥을 항로에 고정한다. 항로 종점과 등대가 같은 X축이어도
	// 완료 시 선체 전체가 등대 위를 덮지 않고, 선수만 목적지에 닿는 모습이 된다.
	const float Bob = FMath::Sin(Time * 2.4f) * 1.6f * Sailing;
	const FVector2D ShipSize(176.0f * Space.Scale, 55.1f * Space.Scale);
	const FVector2D ShipBottom = TrackPointAt(Progress) + FVector2D(0.0f, Bob) * Space.Scale;
	const FVector2D ShipPivot(0.12f, 0.86f);
	const FVector2D ShipPosition = ShipBottom - ShipSize * ShipPivot;
	// 짧은 파형 노이즈보다 전체 원호의 방향을 읽도록 양옆 두 샘플의 간격을 확보한다.
	constexpr float TangentSample = 0.02f;
	const FVector2D TangentStart = TrackPointAt(FMath::Max(0.0f, Progress - TangentSample));
	const FVector2D TangentEnd = TrackPointAt(FMath::Min(1.0f, Progress + TangentSample));
	const FVector2D Tangent = TangentEnd - TangentStart;
	const float LivingRoll = FMath::Sin(Time * 1.9f + Progress * 8.0f) * FMath::DegreesToRadians(1.4f) * Sailing;
	const float ShipAngle = FMath::Clamp(FMath::Atan2(Tangent.Y, Tangent.X) + LivingRoll,
		FMath::DegreesToRadians(-24.0f), FMath::DegreesToRadians(24.0f));
	if (Sailing > KINDA_SMALL_NUMBER)
	{
		// 두 개의 짧은 물결만 재사용해 계산한다. 별도 텍스처/파티클/타이머 상태가 없다.
		const FVector2D Forward(FMath::Cos(ShipAngle), FMath::Sin(ShipAngle));
		const FVector2D Down(-Forward.Y, Forward.X);
		const FVector2D Stern = ShipBottom - Forward * (ShipSize.X * 0.84f);
		TArray<FVector2d> Ripple;
		Ripple.Reserve(7);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const float Age = FMath::Frac(Time / 0.90f + Index * 0.5f);
			const float Alpha = FMath::Sin(Age * PI) * 0.60f * Sailing * Opacity;
			Ripple.Reset();
			for (int32 Point = 0; Point <= 6; ++Point)
			{
				const float U = static_cast<float>(Point) / 6.0f;
				const float Behind = 4.0f + Age * 40.0f + U * (20.0f + Age * 12.0f);
				const float Spread = 5.0f + Age * 13.0f + FMath::Sin(U * PI) * 4.0f;
				Ripple.Add(FVector2d(Stern + (-Forward * Behind + Down * Spread) * Space.Scale));
			}
			FSlateDrawElement::MakeLines(DrawElements, LayerId + 1, Space.Geometry.ToPaintGeometry(), Ripple,
				ESlateDrawEffect::None, FLinearColor(0.88f, 0.96f, 1.0f, Alpha), true, 2.0f * Space.Scale);
		}
	}

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
