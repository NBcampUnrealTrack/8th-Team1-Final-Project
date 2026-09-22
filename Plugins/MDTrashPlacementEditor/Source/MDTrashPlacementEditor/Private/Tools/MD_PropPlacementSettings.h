#pragma once

#include "CoreMinimal.h"
#include "MD_PropPlacementSettings.generated.h"

UENUM()
enum class EMD_PropRotationAxis : uint8
{
	Pitch UMETA(DisplayName = "Pitch"),
	Roll UMETA(DisplayName = "Roll"),
	Yaw UMETA(DisplayName = "Yaw")
};

/** 독립적인 회전축 하나의 설정입니다. 고정 또는 랜덤 각도를 고른 뒤 반전을 적용합니다. */
USTRUCT()
struct FMD_PropRotationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (DisplayName = "Random", ToolTip = "이 축의 각도를 최소~최대 범위에서 선택합니다.\n\n끄면 고정 각도를 사용합니다. 랜덤 각도에 고정 각도를 더하지 않습니다."))
	bool bRandomize = false;

	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (DisplayName = "Angle", EditCondition = "!bRandomize", EditConditionHides, UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg", ToolTip = "랜덤 사용을 끈 상태에서 적용할 각도입니다.\n\n각도 반전을 켜면 이 값의 부호가 뒤집힙니다."))
	double AngleDegrees = 0.0;

	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (DisplayName = "Min Angle", EditCondition = "bRandomize", EditConditionHides, UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg", ToolTip = "랜덤 회전 범위의 최솟값입니다.\n\n최댓값보다 크면 두 값의 순서를 정리합니다."))
	double MinimumDegrees = -180.0;

	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (DisplayName = "Max Angle", EditCondition = "bRandomize", EditConditionHides, UIMin = "-180.0", UIMax = "180.0", ForceUnits = "deg", ToolTip = "랜덤 회전 범위의 최댓값입니다.\n\n최솟값과 같으면 항상 같은 각도를 사용합니다."))
	double MaximumDegrees = 180.0;

	/** 새 난수를 뽑지 않고 선택한 각도의 부호를 뒤집습니다. */
	UPROPERTY(EditAnywhere, Category = "Rotation", meta = (DisplayName = "Invert", ToolTip = "고정값 또는 현재 랜덤값의 부호를 뒤집습니다. 예: 20° → -20°.\n\n새 난수를 뽑지 않으며, 다른 축과 크기는 유지합니다."))
	bool bInvert = false;

	void Sanitize();
	void Rotate(double DeltaDegrees);
	void Freeze(double Fraction);
	double Evaluate(double Fraction) const;
	bool operator==(const FMD_PropRotationSettings& Other) const;
};

/** 속성 패널과 팔레트 항목이 공유하는 배치 설정입니다. */
USTRUCT()
struct FMD_PropPlacementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Orientation", meta = (DisplayName = "Align To Surface", ToolTip = "소품의 위쪽 축을 배치할 표면의 수직 방향에 맞춘 뒤 회전값을 적용합니다.\n\n가구를 수직으로 세워 두려면 끄세요."))
	bool bAlignToSurface = true;

	UPROPERTY(EditAnywhere, Category = "Surface", meta = (DisplayName = "Smart Edge", EditCondition = "bAlignToSurface", ToolTip = "책처럼 얇은 소품은 커서 근처의 모서리와 낮은 바닥을 찾아 기대어 세웁니다. 받침을 찾지 못하면 기존 표면 배치를 사용합니다.\n\n자동 자세는 Pitch와 Roll이 고정 0일 때 적용하며, 방향은 모서리를 기준으로 정합니다. 직접 자세를 지정하려면 이 옵션을 끄거나 Pitch/Roll을 조절하세요. 복잡한 BP는 전체 외형의 Blueprint Preview Mesh를 권장합니다."))
	bool bSmartEdge = true;

	UPROPERTY(EditAnywhere, Category = "Surface", meta = (DisplayName = "Lean Angle", EditCondition = "bSmartEdge && bAlignToSurface", ClampMin = "15.0", ClampMax = "85.0", ForceUnits = "deg", ToolTip = "모서리에 기대는 소품과 바닥 사이의 각도입니다. 90도에 가까울수록 수직에 가깝게 세웁니다.\n\n해당 각도로 놓을 수 없으면 더 낮은 각도도 확인합니다."))
	double LeanAngle = 75.0;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Surface", meta = (DisplayName = "Edge Search Distance", EditCondition = "bSmartEdge && bAlignToSurface", ClampMin = "0.5", ClampMax = "25.0", ForceUnits = "cm", ToolTip = "커서 주위에서 모서리를 찾는 최대 거리입니다. 평평한 면의 중앙에서는 기존 배치를 유지합니다."))
	double EdgeSearchDistance = 5.0;

	UPROPERTY(EditAnywhere, Category = "Orientation", meta = (DisplayName = "Pitch", ToolTip = "Y축을 기준으로 앞뒤로 기울이는 회전입니다.\n\n고정 각도 또는 랜덤 범위와 각도 반전을 지정할 수 있습니다."))
	FMD_PropRotationSettings Pitch;

	UPROPERTY(EditAnywhere, Category = "Orientation", meta = (DisplayName = "Roll", ToolTip = "X축을 기준으로 좌우로 기울이는 회전입니다.\n\n고정 각도 또는 랜덤 범위와 각도 반전을 지정할 수 있습니다."))
	FMD_PropRotationSettings Roll;

	UPROPERTY(EditAnywhere, Category = "Orientation", meta = (DisplayName = "Yaw", ToolTip = "Z축을 기준으로 방향을 돌리는 회전입니다.\n\n고정 각도 또는 랜덤 범위와 각도 반전을 지정할 수 있습니다."))
	FMD_PropRotationSettings Yaw;

	UPROPERTY(EditAnywhere, Category = "Scale", meta = (DisplayName = "Random Scale", ToolTip = "최소~최대 크기 범위에서 배율을 선택합니다.\n\n끄면 균일 크기를 사용합니다. 랜덤 배율에 균일 크기를 다시 곱하지 않습니다."))
	bool bRandomizeScale = false;

	/** 메시의 기본 크기 또는 BP에 설정된 액터 크기에 곱할 배율입니다. */
	UPROPERTY(EditAnywhere, Category = "Scale", meta = (EditCondition = "!bRandomizeScale", EditConditionHides, ClampMin = "0.01", ClampMax = "1000.0", UIMax = "10.0", DisplayName = "Uniform Scale", ToolTip = "소품 전체에 적용할 고정 배율입니다. 1은 원래 크기입니다.\n\n블루프린트는 액터에 설정된 기본 크기에 이 배율을 곱합니다."))
	double UniformScale = 1.0;

	UPROPERTY(EditAnywhere, Category = "Scale", meta = (EditCondition = "bRandomizeScale", EditConditionHides, ClampMin = "0.01", ClampMax = "1000.0", UIMax = "10.0", DisplayName = "Minimum Scale", ToolTip = "랜덤 크기 범위의 최소 배율입니다.\n\n0.01~1000 범위로 지정하며, 최댓값보다 크면 두 값의 순서를 정리합니다."))
	double MinimumScale = 0.9;

	UPROPERTY(EditAnywhere, Category = "Scale", meta = (EditCondition = "bRandomizeScale", EditConditionHides, ClampMin = "0.01", ClampMax = "1000.0", UIMax = "10.0", DisplayName = "Maximum Scale", ToolTip = "랜덤 크기 범위의 최대 배율입니다.\n\n최솟값과 같으면 항상 같은 크기를 사용합니다."))
	double MaximumScale = 1.1;

	/** 회전·크기를 반영해 표면에 맞춘 뒤 수직 방향으로 이동할 거리입니다. */
	UPROPERTY(EditAnywhere, Category = "Surface", meta = (ForceUnits = "cm", DisplayName = "Surface Offset", ToolTip = "회전과 크기를 반영해 표면에 맞춘 뒤, 표면의 수직 방향으로 이동할 거리입니다. 단위는 cm입니다.\n\n양수는 표면 바깥쪽, 음수는 안쪽으로 이동합니다. 음수 값은 겹침 방지로 차단될 수 있습니다."))
	double SurfaceOffset = 0.0;

	UPROPERTY(EditAnywhere, Category = "Randomization", meta = (DisplayName = "Random Seed", ToolTip = "같은 시드와 배치 순서에서는 같은 랜덤 자세를 사용합니다.\n\n배치에 성공하거나 Reroll Preview를 누르면 다음 값으로 진행합니다. 소품을 선택하면 순서가 처음부터 시작됩니다. 현재 항목을 복제할 때는 지금 자세를 이어받습니다."))
	int32 RandomSeed = 1337;

	UPROPERTY(EditAnywhere, Category = "Safety", meta = (DisplayName = "Prevent Overlaps", ToolTip = "미리보기 메시들의 경계 상자로 다른 물체와의 겹침을 검사합니다. 받침 메시의 단순 충돌에 걸리면 정밀 충돌로 접촉을 다시 확인합니다.\n\n선반 내부처럼 메시 안에 빈 공간이 있으면 실제로 들어갈 수 있는 위치도 차단될 수 있습니다."))
	bool bPreventOverlaps = true;

	UPROPERTY(EditAnywhere, Category = "Safety", meta = (EditCondition = "bPreventOverlaps", ClampMin = "0.0", ForceUnits = "cm", DisplayName = "Overlap Tolerance", ToolTip = "맞닿은 배치의 작은 오차를 허용하도록 검사 경계를 줄이는 거리입니다. 단위는 cm입니다.\n\n값을 크게 올리면 실제 관통도 허용할 수 있으므로 기본값 0.1cm부터 조절하세요."))
	double OverlapTolerance = 0.1;

	void Sanitize();
	FMD_PropRotationSettings& GetRotationAxis(EMD_PropRotationAxis Axis);
	bool HasRandomizedValues() const;
	void FreezeRandomValues(uint32 CandidateIndex);
	FQuat EvaluateRotation(uint32 CandidateIndex) const;
	double EvaluateScale(uint32 CandidateIndex) const;
	bool operator==(const FMD_PropPlacementSettings& Other) const;
};
