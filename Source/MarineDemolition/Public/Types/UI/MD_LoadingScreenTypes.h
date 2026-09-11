#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Layout/Geometry.h"
#include "Misc/ScopeLock.h"
#include "Templates/Atomic.h"
#include "MD_LoadingScreenTypes.generated.h"

/**
 * 로딩을 시작한 상황. 화면에 띄울 진행 문구와 완료 문구가 이 값 하나로 정해진다.
 *
 * 호출부가 문구를 직접 넘기지 않는 이유는 두 가지다.
 *   1) 예전에는 진행 FText를 받아 영문 접두사로 짝이 되는 완료 문구를 되찾았다.
 *      호출부의 문구를 한 글자만 고쳐도 완료 문구가 조용히 기본값으로 떨어졌다.
 *   2) 이 값은 ClientPrepareLoadingTransition RPC로 전송된다. 지역화된 문자열 대신
 *      1바이트 열거값을 보내야 서버와 클라이언트의 언어 설정이 달라도 각자 자기 언어로 표시된다.
 */
UENUM(BlueprintType)
enum class EMD_LoadingContext : uint8
{
	/** 상황이 지정되지 않은 일반 로딩. 직접 OpenLevel 같은 fallback 경로가 쓴다. */
	Default,
	/** 세션 생성 / 참가. */
	CrewAssembling,
	/** 세션 시작 - 원정 출발. */
	DiveStarting,
	/** 타이틀로 복귀. */
	ReturningToSurface,
	/** 작업을 중단하고 로비로 복귀. */
	MissionIncomplete,
	/** 로비에서 팀 이탈. */
	LeavingCrew,
	/** 다음 작업일로 전환. */
	NextShift
};

/** 로딩 연출의 진행 단계. 서브시스템은 개별 플래그 대신 이 하나로 상태를 관리한다. */
UENUM()
enum class EMD_LoadingPhase : uint8
{
	/** 연출이 화면에 없다. */
	Inactive,
	/** 화면을 덮는 중. 이 단계에서는 트래블을 시작하지 않는다. */
	Intro,
	/** 화면이 덮인 채 루프. 트래블과 준비 판정이 이 단계에서 일어난다. */
	Loading,
	/** 준비 완료를 알리는 COMPLETE 표시. */
	Completion,
	/** fade-out. 끝나면 위젯과 상태를 모두 되돌린다. */
	Outro
};

/** 인트로가 끝나기 전에 들어온 종료 요청. 인트로 종료 콜백이 소비한다. */
UENUM()
enum class EMD_LoadingFinishRequest : uint8
{
	None,
	/** 정상 완료. COMPLETE 연출을 거쳐 outro로 넘어간다. */
	WithCompletion,
	/** 세션 실패나 준비 판정 타임아웃. COMPLETE 없이 곧바로 outro. */
	WithoutCompletion
};

/** 준비 판정을 막고 있는 원인. 타임아웃 경고 로그에만 쓴다. */
UENUM()
enum class EMD_LoadingReadinessBlocker : uint8
{
	/** 막는 것이 없다. 로딩을 닫아도 된다. */
	None,
	WorldInvalid,
	BeginPlayPending,
	PlayerControllerMissing,
	AsyncLoading,
	LevelVisibilityPending,
	StreamingLevelPending,
	RenderAssetStreaming
};

namespace MD_LoadingScreen
{
	// 연출 순서: Intro 0.90초(화면을 완전히 덮음) → 준비될 때까지 항해 루프
	// → 준비 완료 시 Completion 0.85초 → Outro 1.10초 fade-out.
	constexpr double IntroDurationSeconds = 0.90;
	constexpr double CompletionDisplaySeconds = 0.85;
	constexpr double OutroDurationSeconds = 1.10;
	// 시각 진행률 단계. 실제 바이트 퍼센트는 하드 트래블 중 안전하게 조회할 수 없으므로,
	// 확실히 지나간 단계마다 하한을 올리고 그 사이는 경과 시간으로 메운다.
	/** 인트로가 끝나는 시점의 진행률. 시간 기반 추정이 여기서부터 이어진다. */
	constexpr float IntroProgressShare = 0.08f;
	/** PreLoadMap: 맵 로드가 실제로 시작됐다. */
	constexpr float MapLoadStartProgress = 0.12f;
	/** PostLoadMap: 새 월드가 올라왔다. */
	constexpr float MapLoadedProgress = 0.90f;
	/** 준비 판정이 요구 횟수만큼 연속으로 통과했다. */
	constexpr float ReadyStableProgress = 0.98f;
	/** 블로킹 구간의 시간 기반 추정이 점근하는 상한. 단계 하한 없이는 절대 넘지 않는다. */
	constexpr float BlockingLoadProgressCeiling = 0.88f;
	/** 상한에 접근하는 속도의 시간 상수(초). 클수록 천천히 찬다. */
	constexpr float BlockingLoadProgressTimeConstant = 6.0f;
	/** COMPLETE와 함께 남은 구간을 100%까지 채우는 시간. */
	constexpr double CompletionProgressBlendSeconds = 0.38;

	// 단계 하한은 0.12 → 0.90처럼 크게 뛴다. 그대로 반영하면 배가 트랙의 절반을 한 프레임에
	// 건너뛰므로, 표시값이 목표까지 일정 속도로 따라붙게 한다. 지속 시간이 아니라 속도인 이유:
	// 준비 판정이 0.05초마다 하한을 조금씩 올리는데 그때마다 지속 시간을 다시 잡으면 매번
	// 처음부터 감속해 값이 기어가고, 결국 완료 블렌드가 남은 거리를 한 번에 삼킨다.
	/** 표시 진행률이 목표를 따라붙는 속도(진행률/초). 배의 항해 속도라고 보면 된다. */
	constexpr float FloorCatchUpRate = 0.70f;
	// 하드 트래블 중에는 화면이 통째로 멈춰 1초 넘게 한 프레임도 그려지지 않는다. 정지한
	// 시간을 그대로 더하면 풀리는 순간 한 프레임에 다 이동해 결국 순간이동으로 보이므로,
	// 한 번 그릴 때 나아갈 수 있는 시간에 상한을 둔다(약 30fps 한 프레임).
	/** 한 번의 그리기에서 따라붙을 수 있는 최대 시간(초). */
	constexpr double MaxCatchUpStepSeconds = 0.033;

	static_assert(IntroProgressShare < BlockingLoadProgressCeiling,
		"시간 기반 추정은 인트로가 끝난 값에서 이어져야 한다");
	static_assert(MapLoadStartProgress <= MapLoadedProgress && MapLoadedProgress <= ReadyStableProgress,
		"단계 하한은 진행 순서대로 커져야 한다");

	// 0.05초 x 10회 = 약 0.5초간 조건이 유지돼야 닫는다.
	// 한 프레임만 만족했다가 곧바로 다시 로딩이 시작되는 경우를 거르기 위한 장치다.
	constexpr float ReadinessPollSeconds = 0.05f;
	constexpr int32 RequiredReadyStablePolls = 10;
	// 스트리밍이 영영 해소되지 않을 때 로딩 화면이 무한히 남는 것을 막는 상한.
	constexpr double ReadinessTimeoutSeconds = 45.0;

	// viewport와 MoviePlayer는 스케일 기준이 달라 트래블 순간 폰트/물방울이 튀었다.
	// 양쪽 모두 이 고정 해상도로 계산한 뒤 실제 크기에 맞춰 균일 스케일한다.
	inline const FVector2D DesignSize(1920.0f, 1080.0f);

	/** 게임플레이 UMG(HUD/메뉴)보다 반드시 위에 그려야 하므로 의도적으로 큰 값이다. */
	constexpr int32 ViewportZOrder = 10000;

	/** 타임아웃 경고에 남길 사유. 평상시에는 호출되지 않는다. */
	inline const TCHAR* ToString(EMD_LoadingReadinessBlocker Blocker)
	{
		switch (Blocker)
		{
		case EMD_LoadingReadinessBlocker::WorldInvalid:            return TEXT("World가 유효하지 않음");
		case EMD_LoadingReadinessBlocker::BeginPlayPending:        return TEXT("World가 아직 BeginPlay를 시작하지 않음");
		case EMD_LoadingReadinessBlocker::PlayerControllerMissing: return TEXT("로컬 PlayerController가 아직 생성되지 않음");
		case EMD_LoadingReadinessBlocker::AsyncLoading:            return TEXT("비동기 로딩이 진행 중");
		case EMD_LoadingReadinessBlocker::LevelVisibilityPending:  return TEXT("레벨 visibility 요청이 처리 대기 중");
		case EMD_LoadingReadinessBlocker::StreamingLevelPending:   return TEXT("streaming level 로드/표시 대기");
		case EMD_LoadingReadinessBlocker::RenderAssetStreaming:    return TEXT("render asset streaming 대기");
		default:                                                   return TEXT("없음");
		}
	}
}

/**
 * viewport 위젯과 MoviePlayer 위젯이 함께 참조하는 단일 연출 상태.
 * 트래블 전후로 그리는 주체가 바뀌어도 같은 시간축을 보게 해 연출이 이어진다.
 *
 * 경과 시간이 world time이 아니라 Cycles64인 이유: 하드 트래블 중에는 게임 스레드가
 * LoadMap 안에서 멈춰 world time이 흐르지 않는다. Completion/Outro 시각이 TAtomic인
 * 이유: 게임 스레드가 쓰고 MoviePlayer 로딩 스레드가 읽는다.
 */
struct FMD_LoadingVisualState
{
	FMD_LoadingVisualState(const FText& InSubtitle, const FText& InCompletionSubtitle)
		: Subtitle(InSubtitle), CompletionSubtitle(InCompletionSubtitle), StartCycles(FPlatformTime::Cycles64()) {}

	double GetElapsedSeconds() const { return SecondsSince(StartCycles); }
	void BeginCompletion() { StampOnce(CompletionStartCycles); }
	void BeginOutro() { StampOnce(OutroStartCycles); }
	/**
	 * 실제로 지나간 단계가 진행률의 목표를 올린다. 표시값을 곧바로 갈아끼우지는 않는다.
	 * 실제 이동은 그리는 쪽이 한 프레임씩 따라붙으며 처리한다(AdvanceDisplayedFloor).
	 * 게임 스레드만 호출한다.
	 */
	void SetProgressFloor(float Progress)
	{
		FScopeLock Lock(&FloorMutex);
		FloorTarget = FMath::Max(FloorTarget, FMath::Clamp(Progress, 0.0f, 1.0f));
	}
	/**
	 * MoviePlayer가 게임 스레드를 막은 동안에도 움직이는 단조 증가 시각 진행률.
	 * 실제 바이트 퍼센트는 하드 트래블 중 안전하게 조회할 수 없으므로 경과 시간으로 상한까지
	 * 점근하고, 맵 로드/월드 준비 단계가 올리는 하한이 남은 구간을 확정한다.
	 */
	float GetVisualProgress() const
	{
		float Progress = AdvanceDisplayedFloor();

		// COMPLETE가 뜨면 남아 있던 구간을 채워 100%로 마무리한다.
		const uint64 CompletionStamp = CompletionStartCycles.Load();
		if (CompletionStamp != 0)
		{
			const float CompletionAlpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(static_cast<float>(
				SecondsSince(CompletionStamp) / MD_LoadingScreen::CompletionProgressBlendSeconds), 0.0f, 1.0f));
			Progress = FMath::Lerp(Progress, 1.0f, CompletionAlpha);
		}
		return FMath::Clamp(Progress, 0.0f, 1.0f);
	}

	float GetCompletionElapsedSeconds() const
	{
		const uint64 Stamp = CompletionStartCycles.Load();
		return Stamp == 0 ? 0.0f : static_cast<float>(SecondsSince(Stamp));
	}

	/** outro 진행에 따른 전체 fade-out. outro 전에는 항상 1.0이다. */
	float GetOverallOpacity() const
	{
		const uint64 Stamp = OutroStartCycles.Load();
		if (Stamp == 0) { return 1.0f; }

		const float Alpha = FMath::Clamp(
			static_cast<float>(SecondsSince(Stamp) / MD_LoadingScreen::OutroDurationSeconds), 0.0f, 1.0f);
		return 1.0f - FMath::SmoothStep(0.0f, 1.0f, Alpha);
	}

	FText Subtitle;
	FText CompletionSubtitle;

private:
	static double SecondsSince(uint64 Cycles) { return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Cycles); }
	static void StampOnce(TAtomic<uint64>& Target)
	{
		if (Target.Load() == 0) { Target.Store(FPlatformTime::Cycles64()); }
	}

	/**
	 * 완료 블렌드 이전의 진행률. 그릴 때마다 한 걸음씩 목표에 다가간다.
	 *
	 * 벽시계가 아니라 '그린 횟수'로 나아가는 이유: 하드 트래블 구간에서는 게임 스레드가
	 * LoadMap 안에서 멈춰 1초 넘게 한 프레임도 그려지지 않는다. 그 정지 시간을 그대로
	 * 반영하면 화면이 풀리는 첫 프레임에 이동이 전부 끝나 있어 결국 순간이동으로 보인다.
	 * 그래서 한 걸음의 크기에 상한을 두고, 실제로 그려진 프레임 수만큼만 나아가게 한다.
	 */
	float AdvanceDisplayedFloor() const
	{
		FScopeLock Lock(&FloorMutex);

		const uint64 Now = FPlatformTime::Cycles64();
		const double SinceLastDraw = LastDrawCycles == 0
			? 0.0 : FPlatformTime::ToSeconds64(Now - LastDrawCycles);
		LastDrawCycles = Now;

		// 단계 하한과 시간 추정 중 큰 쪽이 목표다. 시간 추정을 FloorTarget으로 깎으면
		// 표시값이 단계 하한 그 자체(0 → 0.12 → 0.90 계단)가 되고, 하한이 올라가기 전까지
		// 진행률이 한 자리에 멈춰 있다가 다음 단계에서 한 번에 뛴다.
		const float Target = FMath::Max(FloorTarget, GetTimedProgress());
		DisplayedFloor = FMath::Min(Target, DisplayedFloor + MD_LoadingScreen::FloorCatchUpRate
			* static_cast<float>(FMath::Min(SinceLastDraw, MD_LoadingScreen::MaxCatchUpStepSeconds)));
		return DisplayedFloor;
	}

	/**
	 * 단계 정보 없이 경과 시간만으로 추정한 진행률. 인트로 구간에서는 인트로 진척만큼만 차오르고,
	 * 그 뒤에는 상한을 향해 점근하기만 해 상한에 닿지 않는다. 로딩이 길어져도 100%에서
	 * 멈춘 것처럼 보이지 않게 하려는 것이고, 마지막 구간은 실제 단계 하한이 담당한다.
	 */
	float GetTimedProgress() const
	{
		const double Elapsed = GetElapsedSeconds();
		const float IntroProgress = MD_LoadingScreen::IntroProgressShare * FMath::Clamp(
			static_cast<float>(Elapsed / MD_LoadingScreen::IntroDurationSeconds), 0.0f, 1.0f);
		const float LoadingElapsed = FMath::Max(0.0f,
			static_cast<float>(Elapsed - MD_LoadingScreen::IntroDurationSeconds));
		return FMath::Max(IntroProgress, FMath::Lerp(MD_LoadingScreen::IntroProgressShare,
			MD_LoadingScreen::BlockingLoadProgressCeiling,
			1.0f - FMath::Exp(-LoadingElapsed / MD_LoadingScreen::BlockingLoadProgressTimeConstant)));
	}

	uint64 StartCycles;
	TAtomic<uint64> CompletionStartCycles{0};
	TAtomic<uint64> OutroStartCycles{0};

	// 표시값은 그리는 쪽이 갱신하고 목표는 게임 스레드가 올린다. 세 값이 한 벌로 맞아야 해서
	// 개별 atomic으로는 찢어진 읽기를 막을 수 없고, 임계 구역이 산술 몇 줄이라 잠금 비용은
	// 무시할 만하다. viewport와 MoviePlayer 위젯이 같은 프레임에 둘 다 읽어도, 두 번째 호출은
	// 직전 호출과의 간격이 사실상 0이라 걸음이 겹쳐 빨라지지 않는다.
	mutable FCriticalSection FloorMutex;
	float FloorTarget = 0.0f;
	mutable float DisplayedFloor = 0.0f;
	mutable uint64 LastDrawCycles = 0;
};

/**
 * 디자인 해상도(MD_LoadingScreen::DesignSize) 좌표를 실제 위젯 좌표로 옮기는 반응형 변환.
 * 위치는 각 축의 화면 비율을 따르고, 글자·아이콘·선 굵기는 작은 축의 균일 Scale을 쓴다.
 * 따라서 16:9가 아닌 화면에서도 전체 구도는 화면을 채우되 개별 에셋은 찌그러지지 않는다.
 * viewport와 MoviePlayer가 같은 계산을 쓰기 때문에 트래블 순간 위치가 튀지 않는다.
 */
struct FMD_LoadingDesignSpace
{
	explicit FMD_LoadingDesignSpace(const FGeometry& InGeometry)
		: Geometry(InGeometry)
	{
		const FVector2D LocalSize = Geometry.GetLocalSize();
		AxisScale = FVector2D(
			FMath::Max(0.01f, LocalSize.X / MD_LoadingScreen::DesignSize.X),
			FMath::Max(0.01f, LocalSize.Y / MD_LoadingScreen::DesignSize.Y));
		Scale = FMath::Min(AxisScale.X, AxisScale.Y);
	}

	FVector2D Point(const FVector2D& DesignPoint) const { return DesignPoint * AxisScale; }

	const FGeometry& Geometry;
	FVector2D AxisScale = FVector2D(1.0f, 1.0f);
	float Scale = 1.0f;
};
