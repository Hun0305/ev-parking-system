# Parking Mock 데이터·시뮬레이션 모듈화 작업일지

> 작업일: 2026-07-22
> 저장소: `Qt_Client`
> 브랜치: `taejun/rtsp-performance-reconnect`
> 목적: Mock 데이터와 Debug 동작을 운영 Controller에서 분리하고, 자동 테스트 코드는 `tests/` 안에만 유지

## 1. 요청과 문제 정의

팀 리뷰에서 다음 요청이 들어왔다.

> mockdata, 동작을 파일이나 함수로 구분해서 작성 가능할까요?

변경 전에는 `ParkingController`가 다음 세 종류의 책임을 동시에 가졌다.

1. Raspberry Pi REST API 설정, 요청, 재연결
2. 주차 화면 상태와 이벤트 signal 조정
3. 초기 Demo 데이터 생성과 Debug 버튼용 시뮬레이션 동작

이 구조에서는 Controller가 실제 운영 기능인지 테스트 기능인지 빠르게 판별하기 어려웠다. 또한 초기 번호판, 특정 슬롯 점유 목록, 무작위 상태 변경 같은 시연 정책이 운영 Controller 안에 들어 있었다.

이번 작업의 목표는 기능을 제거하는 것이 아니라 책임을 파일과 객체 수준에서 분리하는 것이다.

## 2. 변경 후 책임 경계

```text
ParkingMockData
  ├─ 초기 ParkingViewState 값
  ├─ 초기 Demo 이벤트 값
  └─ 샘플 수신 문자열 값
          │
          ▼
ParkingSimulationService
  ├─ 초기 상태 주입
  ├─ EV/경고/sensor 시뮬레이션
  ├─ 일반 슬롯 무작위 변경
  └─ 샘플·수동 문자열 전달
          │
          ▼
ParkingController
  ├─ 공용 상태 적용
  ├─ 이벤트 기록과 UI signal
  ├─ 문자열 parser
  └─ 실제 REST API 요청·재연결
```

책임 기준은 다음과 같다.

| 모듈 | 알아도 되는 것 | 알면 안 되는 것 |
|---|---|---|
| `ParkingMockData` | Demo 상태 값, 번호판, 샘플 문자열 | UI, 네트워크, Controller 객체 |
| `ParkingSimulationService` | 어떤 시뮬레이션을 언제 적용할지 | API 설정과 JSON 파싱 세부사항 |
| `ParkingController` | 상태 적용, 이벤트, API 연결 | EV-01 Demo 번호판, Mock 점유 목록, 무작위 정책 |
| `MainWindow` | 객체 생성과 signal/slot 조립 | Mock 데이터 값의 세부내용 |
| `tests/` | 기대 결과와 회귀 검증 | 애플리케이션 실행 코드에 테스트 전용 분기 삽입 |

## 3. 추가한 파일

### 3.1 `src/simulation/parkingmockdata.h/.cpp`

Mock 데이터 값만 제공하는 namespace 모듈이다.

- `initialViewState()`
  - `EV-01~EV-16`, `P-01~P-16` 생성
  - EV-01 비전기차 경고와 번호판
  - EV-02 장시간 점유 경고와 번호판
  - P-01, P-03, P-05, P-08, P-11, P-14 초기 점유
- `initialEvents()`
  - 시작 시 표시할 Demo 이벤트 5개 반환
- `sampleIncomingMessages()`
  - Debug의 Run RX sample에서 사용할 정규화 문자열 7개 반환

이 파일은 QObject를 상속하지 않고 Controller 포인터도 보관하지 않는다.

### 3.2 `src/simulation/parkingsimulationservice.h/.cpp`

Mock/Debug 동작을 조정하고 실제 상태 반영은 Controller에 위임하는 서비스다.

- `seedInitialState()`
- `toggleMockEv()`
- `triggerNonEvAlert()`
- `triggerOvertimeAlert()`
- `triggerSensorError()`
- `randomizeParkingSlots()`
- `runSampleMessages()`
- `applyManualMessage()`

`m_mockStep`과 `QRandomGenerator` 사용도 이 서비스로 이동했다.

### 3.3 `tests/parkingsimulationservice_test.cpp`

실제 `ParkingMockData`, `ParkingSimulationService`, `ParkingController`를 조합하여 다음을 검증한다.

- 초기 EV 16개와 General 16개
- 초기 경고와 이벤트 5개
- EV-03 상태 전환
- Hall sensor 오류 시뮬레이션
- 수동 문자열 적용
- 샘플 문자열 일괄 적용

테스트 기대값과 종료 코드는 이 파일 안에만 있으며 `src/`에는 테스트 실행용 조건문을 추가하지 않았다.

## 4. 변경한 운영 파일

### 4.1 `src/controllers/parkingcontroller.h/.cpp`

Controller에서 제거한 책임:

- `initializeMockData()`
- `mockParkingStateFor()`
- `toggleMockEv()`
- `triggerNonEvAlert()`
- `triggerOvertimeAlert()`
- `triggerSensorError()`
- `randomizeParkingSlots()`
- `simulateIncomingMessages()`
- `m_mockStep`

Controller에 추가한 공용 적용 경계:

- `replaceViewState(const ParkingViewState &state)`
- `applyEvSlotUpdate(...)`
- `applyParkingSlotUpdate(...)`

`start()`는 더 이상 Mock 데이터를 만들지 않고 `initializeApiClient()`만 호출한다.

API JSON 파싱 실패 문구도 데이터 출처를 가정하지 않도록 다음과 같이 변경했다.

```text
변경 전: Invalid parking API response | Mock data displayed
변경 후: Invalid parking API response | Previous state retained
```

`processIncomingMessage()`는 제거하지 않았다. 이 함수는 테스트 데이터 자체가 아니라 정규화 메시지를 상태로 바꾸는 공용 parser 프로토타입이기 때문이다. 현재 실제 socket Adapter가 없어서 SimulationService가 이 경계를 호출한다.

### 4.2 `src/models/parkingstate.h/.cpp`

슬롯 시각 상태 변환을 Controller 내부 helper에서 모델 함수로 옮겼다.

```cpp
SlotVisualState deriveSlotVisualState(
    SlotState state,
    bool vehicleTypeKnown,
    bool isEv,
    const QString &alarmText = QString());
```

서버 snapshot과 Mock 데이터가 같은 도메인 변환 규칙을 사용하면서도 Mock 모듈이 Controller 내부 구현에 의존하지 않게 됐다.

### 4.3 `src/mainwindow.h/.cpp`

`MainWindow`가 Composition Root로서 두 객체를 명시적으로 생성한다.

```text
ParkingController 생성
ParkingSimulationService 생성
connectPages()
ParkingSimulationService::seedInitialState()
ParkingController::start()
```

DebugPage의 시뮬레이션 signal은 `ParkingSimulationService`에 연결했다. `Clear alarms`는 실제 상태에 대한 공용 ACK 동작이므로 계속 `ParkingController::clearAlarms()`에 연결한다.

### 4.4 `CMakeLists.txt`

- 앱 target에 `src/simulation/`의 네 파일 추가
- `parkingsimulationservice_test` target과 `parking_simulation_service` CTest 등록
- Windows MinGW AutoMoc predefines 문제를 피하기 위해 새 테스트 target에 `AUTOMOC_COMPILER_PREDEFINES OFF` 적용

## 5. 시작과 데이터 흐름

현재는 기존 시연 동작을 유지하기 위해 앱 시작 시 Mock 상태를 먼저 주입한다.

```text
MainWindow
  → ParkingSimulationService::seedInitialState()
  → ParkingController::replaceViewState()
  → UI에 Demo 상태 표시
  → ParkingController::start()
  → API 연결 시도
```

API 성공 시 서버 snapshot이 현재 상태를 교체한다. API 실패 또는 JSON 파싱 실패 시 데이터 출처와 관계없이 직전 상태를 유지한다.

이 구조적 분리는 “Mock이 앱에서 완전히 비활성화됐다”는 뜻은 아니다. 현재는 Pi 서버 미완료 상황에서도 화면을 시연할 수 있도록 `MainWindow`가 항상 `seedInitialState()`를 호출한다.

## 6. 빌드와 테스트 결과

실행 명령:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

확인 결과:

```text
parkingcontroller_retry .......... Passed
parking_simulation_service ....... Passed
parkingmappage_add_slot .......... Passed
100% tests passed, 0 failed out of 3
```

앱 실행 파일과 새 테스트 실행 파일의 compile/link도 성공했다.

## 7. 이번 작업에서 유지한 동작

- 초기 EV/General 32개 상태
- 초기 번호판과 경고
- Debug 버튼별 대상 슬롯과 메시지
- 무작위 일반 주차면 상태 변경
- 샘플 수신 문자열 7개
- Controller의 API 재연결 동작
- Parking Map의 runtime 상태 전달 형식

즉, 사용자에게 보이는 Demo 동작을 바꾸지 않고 소유 위치만 정리했다.

## 8. 남은 작업

1. 실제 배포에서 초기 Mock 주입을 끌 수 있는 설정 또는 build option 결정
2. Raspberry Pi의 `slot_id`와 Qt `zoneId` 매핑 schema 확정
3. 실제 TLS/TCP 또는 확정 프로토콜 수신 Adapter 구현
4. Adapter가 `replaceViewState()`와 `apply...Update()` 공용 경계를 사용하도록 연결
5. REST snapshot과 실시간 event의 우선순위·동기화 정책 확정

Pi schema가 확정되기 전에는 형식적인 interface/factory를 추가하지 않는다. 실제 교체 경계가 확인되면 `PiServerStateAdapter` 같은 입력 모듈을 `ParkingSimulationService`와 나란히 두는 방향으로 확장한다.

## 9. 결론

이번 변경으로 테스트 코드는 `tests/`, 시연 데이터는 `parkingmockdata.*`, 시연 동작은 `parkingsimulationservice.*`, 운영 상태/API 조정은 `parkingcontroller.*`로 구분됐다.

`ParkingController`라는 이름은 실제 역할과 일치한다. Controller 기능을 가짜로 만들고 테스트 데이터를 대입한 구조가 아니라, 실제 Controller의 공용 입력 경계에 독립된 SimulationService가 Demo 입력을 전달하는 구조다.
