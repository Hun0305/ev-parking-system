# 2026-07-21 Parking Map Add Slot 안정화 작업 기록

## 작업 목적

Parking Map 편집 화면에서 `Add General Slot`, `Add EV Slot`을 사용할 때 발생하던 다음 문제를 정리하고 수정했다.

- 새 General 슬롯의 상태가 `VACANT`와 `GENERAL CAR` 사이에서 일정하지 않게 표시되는 문제
- 채널에 기본 8개 슬롯이 이미 배치된 상태에서 새 슬롯이 기존 슬롯 위에 겹치는 문제
- Add/Delete 및 화면 종료 과정에서 발생할 수 있는 `QGraphicsScene` 수명 관련 크래쉬
- 레이아웃 변경 후 저장되지 않은 상태가 명확히 보이지 않는 문제
- 위 문제를 자동으로 재검증할 UI 회귀 테스트가 없던 문제

## 원인 분석

### 새 General 슬롯 상태가 번갈아 보인 이유

화면의 슬롯 상태는 슬롯 ID를 기준으로 `ParkingViewState`에서 조회한다.

기존 mock 데이터에는 다음 ID가 이미 존재한다.

- EV 슬롯: `EV-01`~`EV-16`
- General 슬롯: `P-01`~`P-16`

기존 `nextZoneId()`는 현재 레이아웃에 같은 ID가 있는지만 확인했다. 따라서 슬롯을 삭제한 후 빈 ID를 다시 사용하거나 저장된 레이아웃에 번호 공백이 있으면, 새 슬롯이 기존 mock/runtime 상태를 그대로 물려받았다.

예를 들어 새로 생성된 슬롯 ID가 mock 데이터에서 점유 중인 `P-03`이면 `GENERAL CAR`, 비어 있는 `P-04`이면 `VACANT`로 표시됐다. Add 동작이 임의로 상태를 번갈아 만드는 것이 아니라, 재사용한 ID에 연결된 기존 상태가 서로 달랐던 것이다.

### 슬롯 겹침 원인

각 채널 패널은 기본적으로 4열 x 2행, 총 8개 슬롯으로 가득 차 있다. 기존 배치 계산은 슬롯 수를 8개 위치에 순환 배치했기 때문에 9번째 슬롯부터 첫 번째 슬롯 위치를 다시 사용했다.

### 종료 시 크래쉬 원인

`ParkingMapPage`가 파괴되는 동안 멤버 데이터가 먼저 정리된 뒤 자식 `QGraphicsScene`이 파괴됐다. 이 과정에서 scene이 `selectionChanged` 신호를 발생시키면 `handleSceneSelectionChanged()`가 이미 파괴 중인 슬롯 데이터를 다시 조회할 수 있었다.

충돌 스택은 다음 흐름을 확인했다.

```text
QGraphicsScene::~QGraphicsScene()
  -> selectionChanged
  -> ParkingMapPage::handleSceneSelectionChanged()
  -> updateAllZoneVisuals()
  -> zoneIndexById()
  -> invalid QString access / SIGSEGV
```

## 반영 내용

### 1. 새 슬롯 ID와 runtime 상태 충돌 방지

`nextZoneId()`가 다음 두 조건을 모두 확인하도록 변경했다.

1. 현재 레이아웃에 같은 ID가 없어야 한다.
2. 마지막으로 수신한 `ParkingViewState`의 EV/General 슬롯에도 같은 ID가 없어야 한다.

따라서 기본 32개 슬롯과 mock 상태가 존재하는 경우 다음 추가 슬롯은 `P-17`, `EV-17`부터 생성된다. 아직 서버 상태가 없는 새 슬롯은 `WAITING DATA`로 표시된다.

### 2. 빈 위치 검색과 staging 영역 추가

`nextZoneRectForChannel()`이 단순 순환 배치 대신 실제 빈 사각형을 찾도록 변경했다.

- 대상 채널 내부의 4열 x 2행 위치를 순서대로 검사한다.
- 기존 슬롯과 여백을 포함해 교차하지 않는 위치만 사용한다.
- 채널에 빈 위치가 없으면 Parking Map 하단의 staging 영역에 배치한다.
- staging 영역은 9열 단위로 확장된다.

staging 슬롯이 화면 밖으로 잘리지 않도록 가장 아래 슬롯 위치에 맞춰 `QGraphicsScene` 높이도 동적으로 확장한다.

화면에는 다음 안내 문구를 표시한다.

```text
NEW SLOT STAGING | Drag into an available channel position
```

새 슬롯을 생성한 뒤에는 `ensureVisible()`을 호출해 사용자가 추가된 슬롯 위치를 바로 볼 수 있게 했다.

### 3. 저장되지 않은 변경 상태 표시

다음 편집 동작 후 상태 라벨에 `Unsaved changes`를 표시한다.

- General/EV 슬롯 추가
- 슬롯 삭제
- 슬롯 이동
- 상세 편집기 필드 변경
- 기본 레이아웃 복원

예시:

```text
Unsaved changes | P-17 added
Unsaved changes | EV-17 moved
Unsaved changes | P-17 deleted
```

단순 슬롯 선택은 레이아웃 변경이 아니므로 `moved` 상태를 표시하지 않도록 분리했다.

### 4. `QGraphicsScene` 종료 수명 보강

`ParkingMapPage` 소멸자를 명시적으로 추가했다.

페이지 파괴가 시작되면 다음 처리를 먼저 수행한다.

- `m_rebuildingScene = true`로 변경
- scene에서 page로 연결된 signal/slot 연결 해제

이후 scene이 선택 해제 신호를 발생시키더라도 파괴 중인 `ParkingMapPage` 슬롯을 다시 호출하지 않는다.

### 5. Add Slot UI 회귀 테스트 추가

`tests/parkingmappage_add_slot_test.cpp`를 추가했다.

테스트 시나리오는 다음과 같다.

1. `EV-01`~`EV-16`, `P-01`~`P-16` runtime 상태 구성
2. 기본 32개 레이아웃 렌더링
3. General/EV 슬롯을 교대로 총 12회 추가
4. 추가된 12개 슬롯이 모두 `WAITING DATA`인지 확인
5. scene 높이가 560보다 커져 staging 영역이 생성됐는지 확인
6. Delete/Add를 20회 반복해 scene 재구성과 선택 수명 검증
7. 페이지 소멸 과정이 크래쉬 없이 끝나는지 확인

CTest에서 GUI 없이 실행할 수 있도록 다음 환경도 설정했다.

- `QT_QPA_PLATFORM=offscreen`
- Qt platform plugin 경로 지정
- 테스트 timeout 30초

## 주요 변경 파일

- `src/pages/parkingmappage.cpp`
- `src/pages/parkingmappage.h`
- `tests/parkingmappage_add_slot_test.cpp`
- `CMakeLists.txt`

## 빌드 및 테스트 결과

실행 명령:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

최종 결과:

```text
Build: success

1/2 parkingcontroller_retry .......... Passed
2/2 parkingmappage_add_slot .......... Passed

100% tests passed, 0 tests failed out of 2
```

최신 실행 파일:

```text
build/smart_parking_qt_client.exe
```

## 최종 동작

- 새 슬롯은 기존 mock/runtime 상태를 잘못 상속하지 않는다.
- 새 슬롯은 서버 상태 수신 전까지 `WAITING DATA`로 표시된다.
- 가득 찬 채널에 새 슬롯을 추가해도 기존 슬롯과 겹치지 않는다.
- 하단 staging 영역에서 새 슬롯을 확인하고 빈 채널 위치로 옮길 수 있다.
- Add/Delete 반복 및 페이지 종료 과정에서 scene 수명 관련 크래쉬가 발생하지 않는다.
- 레이아웃 변경 후 저장 필요 여부를 화면에서 확인할 수 있다.
