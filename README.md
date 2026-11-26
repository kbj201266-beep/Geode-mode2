# GD Toolkit (Geode mod)

기능: Hitbox / Hitbox border / Hitbox trail / Pathfinder / Noclip / Layout mode / Practice music sync /
Speedhack / Physical TPS / Window frame counter. 메뉴는 일시정지 화면(우측 버튼)과 메인 메뉴 하단의 아이콘 버튼으로 엽니다.

## 빌드
1. Geode SDK + CLI 설치 후 `GEODE_SDK` 환경변수 설정
2. (아이콘/효과음을 다시 만들고 싶을 때만) `powershell -ExecutionPolicy Bypass -File tools\gen_assets.ps1`
3. `geode build` → 생성된 `kimyy.gd-toolkit.geode` 를 Geode `mods` 폴더에 복사

## 아직 컴파일/게임 안에서 검증하지 않은 부분 (SDK가 없는 환경에서 작성함)
컴파일 에러가 나면 대부분 아래 중 하나입니다. Geode 바인딩 헤더에서 이름/시그니처만 맞추면 됩니다.
- `GJBaseGameLayer::processCommands(float)` — 2.2081에서 인자가 늘어났을 수 있음 (Hooks.cpp)
- `GJBaseGameLayer::getModifiedDelta(float)` — Physical TPS. 반환값의 의미가 버전마다 달라서 **게임 안에서 TPS 수치로 확인 필요** (Cheats.cpp `modifiedDelta`)
- `m_gameState.m_currentProgress` (프레임 번호), `m_orientedBox->m_p1..m_p4`, `m_levelSettings->m_songOffset`, `m_anticheatSpike`
- Pathfinder: 더미 플레이어를 앞으로 시뮬레이션하는 실험 기능. 포탈/트리거가 실제 게임 상태를 건드릴 수 있어 기본값은 OFF.

## Window frame counter
- Record → 레벨 재시작 후 클리어 → 마지막 클리어 시도가 매크로로 저장됨 (레벨별)
- Count → 각 클릭을 ±1,2,3… 프레임 옮겨 보며 다음 클릭까지 생존하는 범위를 셈 (최대 ±6f, window = early + late + 1)
- 이후 플레이 중 해당 프레임에서 window 숫자와 딩 소리가 나옴

`anticroom.antics-counter.geode`(Antic's Counter)는 컴파일된 바이너리(소스 없음)라서 이 프로젝트에 합치거나 에셋을 가져다 쓰지 않았습니다.
카운터는 같은 개념으로 직접 구현했습니다. 두 모드는 같이 설치해도 충돌하지 않습니다.

## 안전
Safe mode (기본 ON): noclip / speedhack / TPS 변경 중에는 레벨 완료가 기록·제출되지 않습니다.
