# 헤라 왼손목 회전 추종 보정

이전 수정은 왼손 손목 위치를 Grip_L에 붙였지만 손의 component-space 방향은 원래 포즈에 남겨두었다. 팔꿈치/전완만 IK로 움직이면서 아래를 볼 때 손목에 추가 꺾임이 발생했다.

## 적용

- 기존 `/Game/Assets/Character/Shark_MK4/Anim/ABP/ABP_MD_Shark`의 왼손 Two Bone IK에 `bMaintainEffectorRelRot` → `bScraperEquipped` 프로퍼티 바인딩.
- 헤라 장착 시 원래 포즈의 손목–전완 상대 회전을 유지하며 IK 결과를 따라감. 커터에서는 False로 기존 동작 유지.
- 오른손, ToolSocket, Grip_L, 본/메시/손가락 포즈, 애니메이션 시퀀스 변경 없음. 팔꿈치 목표도 기존 값을 유지.
- 백업: `/Game/Revvy/Validation/SharkArmClearance/ABP_MD_Shark_PreWristFollow`.
- 시험본: 같은 폴더 `ABP_MD_Shark_WristTrial`.

## 검증

- 시험본 런타임 노드 반사 조회에서 헤라=True, 커터=False 확인. 바인딩은 일반 노드 핀 목록에 표시되지 않아도 런타임 FAnimNode 값에 적용됨.
- 아래 -70° 같은 자세 비교: 손목의 전완 기준 상대 회전 차43.057°. 이는 보정으로 제거한 IK 추가 회전량이며, 해부학적 굴곡 각도를 의미하지 않는다.
- 원본 반영 후 새 PIE: 오른손 위치 차1.71e-14cm, quaternion 성분 차2.22e-16, 왼손 손목–그립 거리1.17e-13cm.
- 시험본 위 +25° 및 아래 -70° 측정·시각 확인. 손목의 로컬 회전은 원래 애니메이션/기존 보정을 따라 달라질 수 있다.
- 컴파일 오류/경고0, AnimGraph47노드/48연결 유지. 저장 완료, PIE 종료. 전체 도구 어빌리티/멀티플레이/전 프레임 스트레인 검증은 하지 않음.
- 이미지 `wrist_final_down70.png`; 상세 수치 `wrist_final_qa.json`, `wrist_trial_range.json`; 실행·복구 근거 `scraper_wrist_apply.py/json`.
