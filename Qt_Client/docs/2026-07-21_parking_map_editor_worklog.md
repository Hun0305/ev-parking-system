# 2026-07-21 Parking Map Editor Worklog

## 목적

오늘 작업의 핵심은 `Parking Map` 화면을 단순 상태 표시 화면에서 관제자가 직접 주차구역을 만들고 배치할 수 있는 2D 편집형 관제 화면으로 확장하는 것이었다.

현재 테스트 기준은 4채널 구조이다.

- CH1~CH4를 2x2 패널로 표시한다.
- 각 채널은 EV 주차구역 4개와 일반 주차구역 4개를 기본 배치로 가진다.
- 전체 기본 슬롯 수는 EV 16개, 일반 16개, 총 32개이다.
- IVA1~IVA4는 EV 주차구역에만 매핑한다.
- 일반 주차구역은 IVA를 사용하지 않으며 화면에서는 `N/A`로 표시한다.
- 지자기 센서가 아니라 Hall sensor 기준으로 이름과 표시를 정리했다.

## 주요 파일

- `src/pages/parkingmappage.cpp`
- `src/pages/parkingmappage.h`
- `src/models/parkingzonelayout.cpp`
- `src/models/parkingzonelayout.h`
- `src/controllers/parkingcontroller.cpp`
- `src/mainwindow.cpp`
- `src/mainwindow.h`
- `config/parking_map_layout.example.json`
- `.gitignore`
- `CMakeLists.txt`

## 레이아웃 모델

새 모델 `ParkingZoneLayout`을 추가했다.

각 주차구역은 다음 정보를 가진다.

- `zoneId`: `EV-01`, `P-01` 같은 주차구역 ID
- `zoneType`: `EV` 또는 `GENERAL`
- `displayName`: 화면 표시 이름
- `rect`: map scene 기준 `x`, `y`, `width`, `height`
- `rotation`: 회전 각도
- `cameraChannel`: `CH1`~`CH4`
- `ivaAreaId`: EV 구역은 `IVA1`~`IVA4`, 일반 구역은 비움
- `hallSensorId`: Hall sensor 매핑 ID
- `enabled`: 사용 여부

추가한 주요 함수는 다음과 같다.

- `defaultParkingZoneLayout()`
- `loadParkingZoneLayout(...)`
- `saveParkingZoneLayout(...)`

로컬 저장 경로는 `config/parking_map_layout.local.json`이다. 이 파일은 사용자 배치값이므로 Git에는 올리지 않도록 `.gitignore`에 추가했다.

## 기본 배치

기본 배치는 32개 슬롯이다.

- CH1: `EV-01`~`EV-04`, `P-01`~`P-04`
- CH2: `EV-05`~`EV-08`, `P-05`~`P-08`
- CH3: `EV-09`~`EV-12`, `P-09`~`P-12`
- CH4: `EV-13`~`EV-16`, `P-13`~`P-16`

기본 슬롯 크기는 `84 x 58`로 조정했다. 이전보다 세로 길이를 키워서 카드 내부의 ID, 상태, IVA/CH 정보가 더 잘 보이게 했다.

주의할 점:

- `Save layout`을 누른 적이 있으면 `parking_map_layout.local.json`이 우선 로드된다.
- 코드의 기본 배치를 다시 보고 싶으면 `Reset default`를 누르면 된다.
- `Reset default`는 화면 상태만 기본값으로 바꾸며, 파일에 저장하려면 다시 `Save layout`을 눌러야 한다.

## 편집 UX

Parking Map 화면에 다음 조작을 구현했다.

- `Edit layout`
- `Finish edit`
- `Add General Slot`
- `Add EV Slot`
- `Save layout`
- `Reload`
- `Reset default`
- 우클릭 메뉴
  - `Reset to default shape`
  - `Delete zone`

기존에 의미가 모호했던 `Add P` 같은 버튼명은 관제자가 바로 이해할 수 있도록 `Add General Slot`으로 바꾸었다.

`Edit zone`은 별도 편집창을 열지 않고 `Edit layout`을 켜는 동작만 수행해 기능이 중복되고 반응이 없는 것처럼 보였다. 2026-07-22에 해당 메뉴를 제거하고, 편집 진입은 상단 `Edit layout`으로 통일했다.

## 드래그, 크기 조절, 회전

주차칸 그래픽 아이템은 `ParkingZoneGraphicsItem` 커스텀 클래스로 구현했다.

조작 방식은 다음과 같다.

- 슬롯 내부 드래그: 위치 이동
- 좌우 변 근처 hover: 가로 크기 조절
- 상하 변 근처 hover: 세로 크기 조절
- 네 꼭지점 hover: 회전 조작
- 꼭지점 드래그: 사각형 중심 기준 회전

회전축은 좌상단 기준이 아니라 사각형 중심 기준으로 바꿨다.

관련 처리:

- `setTransformOriginPoint(item->rect().center())`
- 크기 변경 후 중심 재설정
- `Reset to default shape` 후 크기 `84 × 58`, 회전 `0°`, 중심 재설정

꼭지점 근처에서는 기본 마우스 커서 대신 직접 그린 회전 커서를 사용한다. Qt 기본 커서에 적절한 회전 모양이 없어서 `QPixmap`과 `QPainter`로 작은 회전 화살표 커서를 생성했다.

## 채널 자동 매핑

슬롯을 다른 채널 패널 안으로 드래그하면 슬롯 중심점을 기준으로 소속 채널을 자동 갱신한다.

예시:

- `EV-02`를 CH1 패널에서 CH2 패널 안으로 이동
- 마우스를 놓는 순간 `cameraChannel`이 `CH2`로 변경
- 오른쪽 상세 패널과 매핑 테이블에도 변경사항 반영

EV 슬롯은 이동한 채널 안에서 가능한 `IVA1`~`IVA4`를 유지하거나 자동 배정한다. 일반 슬롯은 IVA를 비워두며 화면에서는 `N/A`로 표시한다.

## 서버 조회 동작 변경

Parking Map 슬롯을 클릭할 때 서버 상세 조회가 자동으로 발생하던 연결을 제거했다.

이전 동작:

- 슬롯 클릭
- `ParkingMapPage::slotClicked`
- `ParkingController::requestSlotDetail()`
- 서버 연결 또는 조회 시도

현재 동작:

- 슬롯 클릭은 선택과 편집에만 사용한다.
- 서버 조회는 클릭만으로 발생하지 않는다.

이 변경은 편집 중 불필요한 서버 연결 시도를 막기 위한 것이다.

## UI/UX 변경

Football Manager 스타일의 정보 밀도 높은 UI를 참고해서 Parking Map의 시인성을 높였다.

적용한 방향:

- 바닥은 어두운 아스팔트 느낌으로 변경
- 채널 패널은 경기장/보드처럼 구획이 명확하게 보이도록 구성
- EV 주차칸은 파란색 계열 라인 사용
- 일반 주차칸은 노란색 계열 라인 사용
- 점유, 공석, 비EV, 초과, Hall error 상태별 색상 유지
- 주차칸 내부를 작은 카드처럼 구성
  - Zone ID
  - 상태
  - EV는 IVA 표시
  - 일반은 CH 표시
- 선택된 주차칸은 더 밝은 외곽선으로 강조
- 오른쪽 상단에는 `Selected Slot Detail` 패널을 추가
- 오른쪽 하단에는 전체 매핑 테이블 유지

## 슬라이더 변경

`Width`, `Height`, `Rotation`은 숫자 입력칸을 제거하고 슬라이더 방식으로 바꾸었다.

- Width slider: 16~420 px
- Height slider: 16~220 px
- Rotation slider: -180~180 deg
- 현재 값은 읽기 전용 라벨로 표시한다.

`X`, `Y`는 정확한 위치 조정이 필요할 수 있어 숫자 입력으로 유지했다.

## 스크롤과 떨림 보정

작업 중 발견된 문제:

- 하단 스크롤바가 처음부터 가운데에 가까운 위치로 보이는 문제
- 드래그 중 오른쪽 매핑 테이블이 계속 갱신되며 떨리는 문제

처리한 내용:

- `QGraphicsView` 정렬을 좌상단 기준으로 조정
- `showEvent`, `resizeEvent`, reload/reset 후 `scrollMapToOrigin()` 호출
- 드래그 중에는 그래픽 아이템 중심으로 미리보기만 갱신
- 마우스를 놓은 뒤에 상세 패널과 테이블을 갱신

## Mock 상태 확장

`ParkingController`의 mock 상태를 32개 슬롯 기준으로 확장했다.

- EV: `EV-01`~`EV-16`
- General: `P-01`~`P-16`

랜덤 점유 상태 변경 범위도 `P-01`~`P-16`으로 확장했다.

## 빌드와 확인

반복 확인에 사용한 명령:

```powershell
$env:PATH='C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;' + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
```

마지막 확인 기준:

- build 성공
- `parkingcontroller_retry` 테스트 성공
- `build/smart_parking_qt_client.exe` 실행 성공

## 현재 알아둘 점

- 현재 작업은 아직 커밋 전 상태이다.
- `config/parking_map_layout.local.json`은 로컬 사용자 배치 파일이므로 커밋하지 않는다.
- `config/parking_map_layout.example.json`은 기본 예시 파일이므로 커밋 대상이다.
- 앱 실행 중이라면 빌드 전 기존 `smart_parking_qt_client.exe` 프로세스를 종료해야 할 수 있다.

## 다음에 이어서 할 만한 일

- 실제 Pi Server, DB, MQTT 이벤트 수신 시 `zoneId` 기준으로 Parking Map 상태 갱신
- `Save layout` 성공/실패 알림을 종 모양 알림 박스와 연결
- `Unsaved changes` 상태 표시 추가
- 드래그 중 슬롯이 채널 경계를 넘어갈 때 미리 채널 하이라이트 표시
- 주차칸 회전 커서의 hotspot과 모양을 실제 조작감 기준으로 미세 조정
- 실제 테스트 주차장 구조에 맞춰 기본 배치와 차선 스타일 보정
