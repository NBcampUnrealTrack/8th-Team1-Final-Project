# Day3 보안 캐비닛 — 모델 v01

정면 시안을 참고한 열린 문/빈 내부의 1차 모델입니다. AI 시안 이미지가 아닌 실제 Blender 메시입니다.

## 파일
- Day3Cabinet_Working_v01.blend: Blender 작업 원본. 완성 부품은 `D3_` 이름 50개이며 작업 원점은 X=4 m입니다. 사용자 Cube와 제작 중간본은 보존되어 있습니다. 중간본은 X=-20 m 이하에 따로 있습니다. 전체 선택 내보내기를 하지 말고 `D3_` 부품만 사용하세요.
- Day3Cabinet_OpenEmpty_v01.fbx: 완성 부품 50개만 포함. 본체 하단 중앙이 원점이며 Z가 위입니다.
- Day3Cabinet_OpenEmpty_v01.glb: 완성 부품과 7개 재질. glTF 좌표계는 Y-up입니다.
- ModelPreview_Front_v01.png / ModelPreview_Perspective_v01.png: 내보낸 GLB 삼각형을 직접 투영한 검수 이미지. Blender/Cycles 렌더가 아니며 조명·그림자는 단순화했습니다.

## 구조와 크기
- 본체 폭 1.10 m × 깊이 0.75 m × 높이 1.75 m. 열린 문은 이 범위를 벗어납니다.
- 청록 본체, 아이보리 문틀, 카드 리더기, 검은 패킹, 투명 유리, 빈 받침대.
- Door/Glass/Handle 이름의 문 부품은 같은 힌지 원점(-0.455, -0.415, 0 m)과 Z=-110도 회전을 사용합니다. 현재 서로 부모 연결되지 않았으므로 애니메이션/BP에서는 하나의 문 컴포넌트 아래 묶어야 합니다.
- 샘플은 포함하지 않습니다. X-7 로고·경고 데칼·사용감 텍스처는 미포함입니다.
- LED는 색상 재질입니다. 실제 발광, 카드 인식, 문 개폐 게임 로직, 충돌, Unreal 임포트는 미적용입니다.

## 검증
- 완성 메시 50개, 삼각형 15,000개, 재질 7개.
- 각 부품의 degenerate faces / non-manifold edges / non-finite vertices / degenerate UV faces: 0.
- 메시 검증 blocker: 0. GLB 파싱 및 좌표 유한성 확인.
- 유리 재질 GLB alphaMode=BLEND, alpha=0.2 확인.
- 원본 Cube fingerprint를 보존하며 복제본으로만 제작.
- 최초 백업: C:/Users/TaitoP/AppData/Local/Revvy/BlenderExchange/Day3Cabinet_20260930/before.blend
- 상세 도구 결과: Saved/Diagnostics/Day3CabinetModel_20260930. 얇은 패널의 초기 베벨 오류본은 완성본/내보내기에 포함하지 않았습니다.
- 맵 수정·커밋·푸시 없음.

## Unreal 적용 후속 작업
2026-09-30: 본체/닫힌 문/유리용 FBX를 추가로 내보내고 Unreal 캐비닛 BP를 생성했습니다. Unreal 에셋에는 카드 판정, 문 개폐, 내부 샘플 회수와 발광 재질을 적용했습니다. 원래 모델 v01 파일 자체는 위 사양 그대로이며, 실제 게임 사용은 Docs/Day3_보안캐비닛_배치.md를 참고하세요.
