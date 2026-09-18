# 헤라 아래보기 팔꿈치 보정

2026-09-18. 사용자 승인: 오른손/헤라 방향을 유지하면서 양손과 헤라를 앞·아래로 이동해 팔꿈치를 펴기.

## 적용

- 기존 `/Game/Assets/Character/Shark_MK4/Anim/ABP/ABP_MD_Shark`에 적용·저장.
- 백업 `/Game/Revvy/Validation/SharkArmClearance/ABP_MD_Shark_PreElbowReach`.
- 검증용 `/Game/Revvy/Validation/SharkArmClearance/ABP_MD_Shark_ElbowReachTrial`.
- 기존 오른손 작업 IK/회전 보정 뒤, 왼손 그립 IK 앞에 오른손 TwoBoneIK 추가.
- 손목 본 공간 effector offset `(-3.2428469144,-2.8426089895,-2.5305173429)`. 최대 아래보기에서 컴포넌트 공간 앞 +3cm, 아래 -4cm; 캐릭터 스케일1.5에서는 앞4.5cm/아래6cm.
- 기존 팔꿈치 위치를 pole로 사용. Stretch 비활성화. 오른손 컴포넌트 회전 보존. 왼손은 기존 hand_r 공간 Grip_L 타깃과 손목 회전 유지 로직 사용.
- Alpha = MapRangeClamped(AimPitch,6..42,0..1) × scraper-equipped × (1-clamp(ScrapeRightHandIKAlpha,0,1)) × NotSwimmingAlpha.
- 기존 AimPitch 보간을 따름. 위보기/정면/다른 도구에는 추가 reach 없음. 실제 작업 IK 활성 시 추가 reach를 낮추도록 연결.
- 메시, 스켈레톤, 소켓, 시퀀스, 머티리얼, C++ 변경 없음.

## 검증

| 포즈 | 왼팔꿈치 내각 | 오른팔꿈치 내각 | 그립 오차 |
|---|---:|---:|---:|
| 아래70도 이전 | 120.63° | 118.38° | <1e-12cm |
| 아래30도 시험본 | 144.79° | 125.36° | <1e-12cm |
| 아래60/70도 보정본 | 161.20° | 133.36° | <1e-12cm |

180도는 완전히 편 상태. 좌우 상완14.2225cm/전완11.6795cm로 유지되어 테스트 포즈에서 팔 길이 늘어남 없음. 아래70도 오른손 quaternion 성분 변화 최대1.67e-16. 정면 원본/시험본 6개 팔 본의 위치·회전 차이0.

기존 경로를 읽는 새 PIE 세션에서 재검증. 1인칭 리더 포즈 팔꿈치/손목 위치 차이0. 커터 장착 및 위25도에서 추가 IK Alpha0 확인. AnimGraph 58노드/58연결, 컴파일 오류0/경고0. PIE 종료.

스킬 절차에 따라 복제본 시험 → 포즈 측정/스크린샷 → 원본 백업 → 적용 → 새 PIE 검증 순서로 진행.

## 증거와 한계

- `elbow_final_down70.png`: 최종 측면. `elbow_trial_down30.png`, `elbow_trial_down60.png`.
- `elbow_final70.json`, `elbow_zero_compare.json`, `elbow_cutter_gate.json`, `elbow_up25_gate.json`, `elbow_apply_result.json`.
- `elbow_final_fp70.png`: 1인칭 확인; 기존 어깨 디더와 손가락 포즈는 수정하지 않음.
- 실제 벽 긁기 어빌리티 전체 흐름, 수영 전환, 멀티플레이, 모든 이동 애니메이션 전 구간은 검증하지 않음. 작업 IK 우선권은 그래프 연결로 확인했으며 작업 중 시각적 검증을 의미하지 않음.
- 기존 왼손 IK의 MaxStretchScale4 설정은 타 작업 영향을 피하려고 유지. 이번 테스트 포즈에서는 스트레칭이 발생하지 않음.
- 스크립트는 작업 기록이며 일괄 재실행용 아님. `elbow_reach_apply.py`는 기존 라이브 `add_reach_gate` 함수를 사용하며 백업 존재 시 중단함.
