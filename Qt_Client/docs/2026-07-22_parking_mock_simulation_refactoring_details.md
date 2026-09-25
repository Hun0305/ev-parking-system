# Parking Mock·Simulation 모듈 분리 상세 설계서

> 작성일: 2026-07-22
> 대상 저장소: `Qt_Client`
> 대상 브랜치: `taejun/rtsp-performance-reconnect`
> 변경 범위: 주차 상태 Mock 데이터, Debug 시뮬레이션, `ParkingController`, 앱 조립, 자동 테스트
> 문서 목적: 이번 리팩터링의 이유와 결과를 처음 보는 팀원도 코드 수정이 가능하도록 파일·클래스·함수 단위로 설명

---

## 1. 작업 요청

팀에서 다음 요청이 전달됐다.

> mockdata, 동작을 파일이나 함수로 구분해서 작성 가능할까요?

추가 요구사항은 다음과 같이 해석했다.

1. Mock 값과 실제 Controller 기능을 같은 함수 안에 두지 않는다.
2. Mock 데이터와 Mock 동작도 서로 다른 파일 또는 함수 책임으로 나눈다.
3. 자동 테스트의 검증 코드와 기대값은 `tests/`에서 관리한다.
4. 실제 API 연결, 상태 모델, 이벤트 signal은 운영 모듈에 유지한다.
5. Pi 서버가 아직 준비되지 않은 현재 시연 동작은 깨뜨리지 않는다.
6. 실제 Pi 연동 시 Controller를 다시 만들지 않고 입력 Adapter만 연결할 수 있게 한다.

---

## 2. 결론

이번 변경 후 코드는 다음 네 영역으로 구분된다.

| 영역 | 파일 | 책임 |
|---|---|---|
| 운영 상태/API | `src/controllers/parkingcontroller.*` | API 연결, 상태 적용, 이벤트, UI signal |
| 도메인 상태 변환 | `src/models/parkingstate.*` | 상태 enum과 시각 상태 변환 |
| Mock 값 | `src/simulation/parkingmockdata.*` | 초기 상태, 초기 이벤트, 샘플 문자열 |
| Mock/Debug 동작 | `src/simulation/parkingsimulationservice.*` | Mock 값을 Controller 공용 입력에 전달 |
| 자동 검증 | `tests/parkingsimulationservice_test.cpp` | 상태·동작 회귀 테스트 |

`ParkingController`는 실제 운영 Controller다. 가짜 Controller에 테스트 데이터를 대입한 구조가 아니다.

현재 구조는 실제 Controller의 공용 상태 입력 경계에 독립적인 `ParkingSimulationService`가 Demo 입력을 전달하는 방식이다.

---

## 3. 변경 전 구조와 문제점

변경 전 `ParkingController`는 다음 책임을 모두 가지고 있었다.

```text
ParkingController
  ├─ API 설정 읽기
  ├─ HTTP/HTTPS GET 요청
  ├─ JSON snapshot 적용
  ├─ 재연결 backoff
  ├─ 화면 상태 저장
  ├─ 이벤트 signal
  ├─ 초기 Mock 32개 생성
  ├─ Mock 번호판과 경고 생성
  ├─ EV-03 전환 단계 보관
  ├─ 일반 주차면 randomize
  └─ 샘플 수신 메시지 목록 보관
```

### 3.1 운영 책임과 Demo 정책 혼합

`ParkingController::start()`가 실제 API 초기화 전에 `initializeMockData()`를 호출했다.

따라서 Controller만 읽어서는 다음을 구별하기 어려웠다.

- 실제 서버에서 들어오는 상태
- 화면 시연을 위해 코드가 생성한 상태
- Debug 버튼이 발생시킨 상태
- parser 자체의 공용 처리

### 3.2 구체적인 테스트 값이 운영 Controller에 존재

다음 값이 `parkingcontroller.cpp` 안에 있었다.

- 번호판 `12A3456`
- 번호판 `34B7788`
- 번호판 `56C9012`
- 초기 점유 P slot 번호 목록
- 샘플 문자열 7개
- `m_mockStep`
- `QRandomGenerator`

이 값들은 API 연결이나 상태 조정 정책과 관계없는 Demo 값이다.

### 3.3 변경 영향 범위가 불필요하게 큼

Mock 번호판 하나를 바꾸거나 Debug 시나리오를 추가해도 운영 Controller를 수정해야 했다. 반대로 API 재연결 코드를 수정할 때도 Mock 함수가 같은 파일의 diff에 함께 나타났다.

### 3.4 리뷰 시 역할 오해 가능성

`ParkingController` 안에 Mock 동작이 많아 다음과 같은 질문이 발생할 수 있었다.

> Controller라는 이름만 사용한 테스트 기능인가?

실제로는 운영 Controller였지만, 파일의 책임이 섞여 있어 코드를 처음 본 사람이 그렇게 오해할 여지가 있었다.

---

## 4. 변경 후 전체 소프트웨어 구조

```text
[외부 연동 계층]
Raspberry Pi Server
  └─ 현재 REST prototype
       ↓
     ApiClient
       └─ ParkingResponseParser
              │
              ↓
[Application 계층]
ParkingController
  ├─ ParkingViewState 소유
  ├─ API·재연결 조정
  ├─ 상태 적용
  └─ UI signal 발생
       ↑
       │ 상태 적용 요청
[Simulation 계층]
ParkingMockData ──→ ParkingSimulationService
                         ↑
                         └─ DebugPage의 Mock/Debug 요청

[UI 조립 계층]
MainWindow
  ├─ ParkingController 생성·연결
  ├─ ParkingSimulationService 생성·연결
  ├─ ParkingMapPage
  ├─ DashboardPage
  ├─ EventsPage
  └─ NotificationCenter
```

### 4.1 의존성 방향

의존성은 다음 방향을 따른다.

```text
ParkingMockData
    ↓
ParkingSimulationService
    ↓
ParkingController
    ↓
ParkingViewState / UI signals
```

반대 방향 의존성은 만들지 않았다.

- `ParkingController`는 `ParkingMockData`를 include하지 않는다.
- `ParkingController`는 `ParkingSimulationService`를 알지 못한다.
- `ParkingMockData`는 Controller 포인터를 가지지 않는다.
- `ParkingMockData`는 UI와 Qt Network를 사용하지 않는다.
- 자동 테스트 파일은 앱 target에 compile되지 않는다.

### 4.2 역할 분리 원칙

```text
값을 정의한다        → ParkingMockData
값을 언제 적용한다   → ParkingSimulationService
상태를 실제로 반영한다 → ParkingController
상태의 의미를 변환한다 → parkingstate
객체를 연결한다      → MainWindow
결과를 검증한다      → tests
```

---

## 5. 변경 파일 전체 목록

### 5.1 새로 추가한 소스

```text
src/simulation/parkingmockdata.h
src/simulation/parkingmockdata.cpp
src/simulation/parkingsimulationservice.h
src/simulation/parkingsimulationservice.cpp
tests/parkingsimulationservice_test.cpp
```

### 5.2 수정한 소스

```text
CMakeLists.txt
src/controllers/parkingcontroller.h
src/controllers/parkingcontroller.cpp
src/mainwindow.h
src/mainwindow.cpp
src/models/parkingstate.h
src/models/parkingstate.cpp
```

### 5.3 관련 문서

```text
docs/2026-07-22_qt_client_module_implementation_guide.md
docs/2026-07-22_parking_simulation_modularization_worklog.md
docs/2026-07-22_parking_mock_simulation_refactoring_details.md
```

---

## 6. `ParkingMockData` 상세

### 6.1 파일

```text
src/simulation/parkingmockdata.h
src/simulation/parkingmockdata.cpp
```

### 6.2 목적

`ParkingMockData`는 Mock에서 사용하는 구체적인 값만 생성한다.

namespace 기반으로 작성했으며 QObject가 아니다. 상태를 어디에 적용할지 결정하지 않고 단순히 값을 반환한다.

### 6.3 `EventRecord`

```cpp
struct EventRecord {
    QString zone;
    QString eventType;
    QString message;
    QString status;
};
```

초기 Demo 이벤트를 Controller의 `recordEvent()`에 넘기기 위한 값 객체다.

| 필드 | 의미 | 예시 |
|---|---|---|
| `zone` | 이벤트 발생 영역 | `EV-01` |
| `eventType` | 이벤트 종류 | `NON_EV_ALERT` |
| `message` | 사용자 표시 설명 | `Non-EV vehicle detected...` |
| `status` | 이벤트 상태 | `OPEN` |

### 6.4 `initialViewState()`

반환형:

```cpp
ParkingViewState initialViewState();
```

역할:

1. 빈 `ParkingViewState` 생성
2. `EV-01~EV-16` 기본 상태 생성
3. EV-01과 EV-02에 Demo 경고 적용
4. `P-01~P-16` 기본 상태 생성
5. 일부 일반 주차면을 점유 상태로 설정
6. 각 slot의 `SlotVisualState` 계산
7. 완성된 상태를 값으로 반환

초기 EV 상태:

| Slot | 점유/경고 | 번호판 | EV 여부 | 점유시간 |
|---|---|---|---|---|
| EV-01 | `NON_EV_ALERT` | `12A3456` | false | `00:18` |
| EV-02 | `OVERTIME_ALERT` | `34B7788` | true | `03:42` |
| EV-03~EV-16 | `VACANT` | `-` | 기본 true | `00:00` |

초기 General 상태:

```text
OCCUPIED: P-01, P-03, P-05, P-08, P-11, P-14
VACANT: 그 외 P-01~P-16
```

### 6.5 `initialEvents()`

반환형:

```cpp
QList<EventRecord> initialEvents();
```

반환 이벤트:

1. EV-01 비전기차 경고
2. EV-02 장시간 점유 경고
3. P-01 점유 이벤트
4. P-02 공석 이벤트
5. P-05 점유 이벤트

이 함수는 signal을 발생시키지 않는다. 이벤트 적용은 `ParkingSimulationService::seedInitialState()`가 Controller에 위임한다.

### 6.6 `sampleIncomingMessages()`

반환형:

```cpp
QStringList sampleIncomingMessages();
```

현재 샘플 목록:

```text
PARKING_SLOT,P01,OCCUPIED
PARKING_SLOT,P02,VACANT
EV_ALERT,EV01,NON_EV
EV_ALERT,EV02,OVERTIME
FIRE_ALARM,CH2,DETECTED
HALL_SENSOR,P03,ERROR
EVENT,CH1,CAMERA_DISCONNECTED,FAILED,RTSP stream disconnected
```

샘플 목록은 parser 구현과 분리되어 있다. 메시지 형식의 파싱은 Controller가 담당한다.

---

## 7. `ParkingSimulationService` 상세

### 7.1 파일

```text
src/simulation/parkingsimulationservice.h
src/simulation/parkingsimulationservice.cpp
```

### 7.2 목적

Mock/Debug 기능의 동작 순서를 소유한다.

이 클래스는 상태를 직접 저장하지 않는다. 실제 상태는 `ParkingController`가 소유하며 SimulationService는 다음 공용 함수로 요청한다.

```cpp
replaceViewState(...)
applyEvSlotUpdate(...)
applyParkingSlotUpdate(...)
processIncomingMessage(...)
recordEvent(...)
```

### 7.3 생성자

```cpp
ParkingSimulationService(
    ParkingController *controller,
    QObject *parent = nullptr);
```

주입받는 Controller를 통해서만 상태를 변경한다. Controller가 null이면 각 동작은 즉시 반환한다.

### 7.4 `seedInitialState()`

동작:

```text
ParkingMockData::initialViewState()
    ↓
ParkingController::replaceViewState()
    ↓
ParkingMockData::initialEvents()
    ↓
각 이벤트를 ParkingController::recordEvent()
```

초기 상태 생성과 초기 이벤트 기록을 한 번에 실행하는 시작용 시뮬레이션 함수다.

### 7.5 `toggleMockEv()`

대상: `EV-03`

`m_mockStep`을 증가시키고 홀수/짝수에 따라 상태를 전환한다.

| 단계 | 상태 | 번호판 | 점유시간 |
|---|---|---|---|
| 홀수 | `VACANT` | `-` | `00:00` |
| 짝수 | `OCCUPIED` | `56C9012` | `00:07` |

상태 적용 후 `OCCUPIED` 또는 `VACANT` 이벤트를 기록한다.

`m_mockStep`은 Controller에서 제거되고 이 서비스로 이동했다.

### 7.6 `triggerNonEvAlert()`

대상: `EV-01`

```text
state = NonEvAlert
plate = 12A3456
isEv = false
occupiedTime = 00:19
alarmText = NON_EV_ALERT
```

상태 적용 후 `NON_EV_ALERT / OPEN` 이벤트를 기록한다.

### 7.7 `triggerOvertimeAlert()`

대상: `EV-02`

```text
state = OvertimeAlert
plate = 34B7788
isEv = true
occupiedTime = 03:43
alarmText = OVERTIME_ALERT
```

상태 적용 후 `OVERTIME_ALERT / OPEN` 이벤트를 기록한다.

### 7.8 `triggerSensorError()`

대상: `P-03`

`ParkingController::applyParkingSlotUpdate()`를 통해 `SensorError` 상태를 적용하고 `HALL_SENSOR_ERROR / OPEN` 이벤트를 기록한다.

### 7.9 `randomizeParkingSlots()`

대상: `P-01~P-16`

각 slot에 대해 `QRandomGenerator`로 `Occupied` 또는 `Vacant`를 선택한다.

각 변경은 다음 두 작업을 수행한다.

1. `applyParkingSlotUpdate()`로 상태 적용
2. `recordEvent()`로 변경 이벤트 기록

`QRandomGenerator` include와 randomize 정책은 Controller에서 제거됐다.

### 7.10 `runSampleMessages()`

```text
ParkingMockData::sampleIncomingMessages()
    ↓
각 QString 반복
    ↓
ParkingController::processIncomingMessage(message)
```

샘플 데이터와 실행 동작을 나눈 대표적인 경계다.

### 7.11 `applyManualMessage()`

DebugPage에서 사용자가 직접 입력한 문자열을 Controller parser로 전달한다.

SimulationService는 문자열 형식을 해석하지 않는다.

---

## 8. `ParkingController` 변경 상세

### 8.1 유지한 핵심 책임

`ParkingController`는 다음 실제 애플리케이션 책임을 계속 가진다.

- `ParkingViewState` 소유
- API 설정 로드
- API client와 image loader 수명 관리
- snapshot 요청
- JSON DTO를 화면 상태로 적용
- API 실패 재연결과 backoff
- 정규화 문자열 parser
- 이벤트 signal
- banner와 server connection signal
- 슬롯 상세 이미지 경로 관리
- 알람 ACK 처리

### 8.2 제거한 함수와 멤버

```text
initializeMockData()
mockParkingStateFor()
toggleMockEv()
triggerNonEvAlert()
triggerOvertimeAlert()
triggerSensorError()
randomizeParkingSlots()
simulateIncomingMessages()
m_mockStep
```

Controller의 include에서도 `QRandomGenerator`를 제거했다.

### 8.3 `start()`

변경 전:

```text
initializeMockData()
initializeApiClient()
```

변경 후:

```text
initializeApiClient()
```

Controller 시작 함수는 이제 운영 초기화만 수행한다.

### 8.4 `replaceViewState()`

```cpp
void replaceViewState(const ParkingViewState &state);
```

전체 상태를 값으로 교체하고 `notifyStateChanged()`를 호출한다.

현재는 시작 Demo 상태 주입에 사용한다. 향후 전체 snapshot Adapter나 테스트 fixture에서도 동일한 공용 경계를 사용할 수 있다.

### 8.5 `applyEvSlotUpdate()`

```cpp
void applyEvSlotUpdate(
    const QString &slotId,
    SlotState state,
    const QString &plateNumber,
    bool isEv,
    const QString &occupiedTime,
    const QString &alarmText);
```

동작:

1. `EvSlotInfo` 생성
2. ACK 상태면 기존 visual 상태를 유지하면서 `alarmAcknowledged=true`
3. 그 외 상태면 `deriveSlotVisualState()` 호출
4. `m_state.evSlots[slotId]` 교체
5. `notifyStateChanged()` 호출

데이터 출처가 API인지 Simulation인지 구분하지 않는다.

### 8.6 `applyParkingSlotUpdate()`

```cpp
void applyParkingSlotUpdate(
    const QString &slotId,
    SlotState state);
```

일반 주차면 상태를 적용한다.

SensorError에서 ACK로 전환할 때 기존 visual 경고 정보를 유지하고 ACK 여부만 변경하는 기존 동작을 보존했다.

### 8.7 `processIncomingMessage()`를 유지한 이유

이 함수는 샘플 데이터를 생성하지 않는다. 전달받은 정규화 문자열을 해석하는 parser다.

지원 형식:

```text
PARKING_SLOT
HALL_SENSOR
HALL_SENSOR_EVENT
EV_ALERT
FIRE_ALARM
EVENT
```

현재 실제 socket receiver가 없으므로 Debug 입력을 통해 호출된다. 향후 Pi Adapter가 같은 메시지 규격을 사용한다면 이 함수를 호출할 수 있다.

따라서 이 함수는 테스트 전용이 아니라 외부 입력 처리 프로토타입으로 Controller에 유지했다.

### 8.8 API 파싱 실패 문구 변경

변경 전 문구:

```text
Invalid parking API response | Mock data displayed
```

변경 후 문구:

```text
Invalid parking API response | Previous state retained
```

Controller가 현재 상태의 출처를 알지 못하도록 문구도 일반화했다.

---

## 9. `parkingstate` 모델 변경

### 9.1 추가 함수

```cpp
SlotVisualState deriveSlotVisualState(
    SlotState state,
    bool vehicleTypeKnown,
    bool isEv,
    const QString &alarmText = QString());
```

### 9.2 이동 이유

기존에는 Controller 내부 anonymous helper인 `makeSlotVisualState()`가 변환을 담당했다.

MockData에서도 같은 규칙이 필요했지만, MockData가 Controller 내부 helper에 의존할 수는 없다. 변환 규칙은 상태 모델의 의미에 해당하므로 `parkingstate.*`로 이동했다.

### 9.3 변환 규칙

```text
Vacant
  → occupancy = Vacant

SensorError
  → occupancy = Unknown
  → alarm = SensorError

그 외 점유/경고 상태
  → occupancy = Occupied
  → vehicleTypeKnown이면 Electric 또는 General

Acked
  → alarmAcknowledged = true
```

서버 snapshot과 Simulation 데이터는 동일한 변환 함수를 사용한다.

---

## 10. `MainWindow` 조립 변경

### 10.1 멤버 추가

```cpp
ParkingSimulationService *m_parkingSimulationService = nullptr;
```

### 10.2 생성 순서

```cpp
buildUi();
m_parkingController = new ParkingController(...);
m_parkingSimulationService =
    new ParkingSimulationService(m_parkingController, this);
connectPages();
m_parkingSimulationService->seedInitialState();
m_parkingController->start();
```

### 10.3 순서의 의미

1. UI를 먼저 만든다.
2. 실제 상태/API Controller를 만든다.
3. Controller를 사용하는 SimulationService를 만든다.
4. signal/slot을 모두 연결한다.
5. 초기 Demo 상태를 주입한다.
6. 실제 API 연결을 시작한다.

signal 연결 뒤 초기 상태를 넣기 때문에 Dashboard, Parking Map, Events, NotificationCenter가 초기 이벤트를 받을 수 있다.

### 10.4 Debug signal 연결

| DebugPage signal | 새 연결 대상 |
|---|---|
| `clearAlarmsRequested` | `ParkingController::clearAlarms` |
| `toggleMockEvRequested` | `ParkingSimulationService::toggleMockEv` |
| `nonEvAlertRequested` | `ParkingSimulationService::triggerNonEvAlert` |
| `overtimeAlertRequested` | `ParkingSimulationService::triggerOvertimeAlert` |
| `sensorErrorRequested` | `ParkingSimulationService::triggerSensorError` |
| `randomizeParkingRequested` | `ParkingSimulationService::randomizeParkingSlots` |
| `sampleMessagesRequested` | `ParkingSimulationService::runSampleMessages` |
| `manualMessageRequested` | `ParkingSimulationService::applyManualMessage` |

`clearAlarms()`는 Mock 생성 동작이 아니라 현재 상태의 경고를 ACK 처리하는 공용 기능이므로 Controller 연결을 유지했다.

---

## 11. 실행 시 동작 순서

### 11.1 앱 시작

```text
MainWindow
  ├─ 1. ParkingController 생성
  ├─ 2. ParkingSimulationService 생성
  │      └─ ParkingController 포인터 주입
  ├─ 3. connectPages()
  ├─ 4. ParkingSimulationService::seedInitialState()
  │      ├─ ParkingMockData::initialViewState()
  │      │      └─ ParkingViewState 반환
  │      ├─ ParkingController::replaceViewState(state)
  │      │      └─ stateChanged → Parking Map / Dashboard 갱신
  │      └─ ParkingMockData::initialEvents()
  │             └─ 이벤트 5개를 recordEvent()로 전달
  └─ 5. ParkingController::start()
         └─ ApiClient 첫 snapshot 요청
```

### 11.2 API 연결 성공

```text
ApiClient GET 성공
    ↓
ParkingResponseParser::parseSnapshot()
    ↓
ParkingController::resetSlotsForSnapshot()
    ↓
알려진 EV-/P- ID 적용
    ↓
기존 Demo runtime 상태를 서버 snapshot 상태로 교체
    ↓
stateChanged + Connected
```

### 11.3 API 연결 실패

```text
GET timeout 또는 network error
    ↓
requestFailed
    ↓
API_ERROR 이벤트
    ↓
직전 상태 유지
    ↓
retry in N seconds
    ↓
single-shot QTimer
```

실패 처리에는 더 이상 `Mock`이라는 데이터 출처 가정이 없다.

### 11.4 Debug sample 실행

```text
사용자
  └─ Run RX sample 클릭
       ↓
DebugPage::sampleMessagesRequested
       ↓
ParkingSimulationService::runSampleMessages()
       ↓
ParkingMockData::sampleIncomingMessages()
       └─ QStringList 반환
              ↓
        각 문자열 반복
              ↓
ParkingController::processIncomingMessage(message)
  ├─ 상태 적용
  ├─ recordEvent()
  ├─ stateChanged
  └─ eventLogged
       ↓
Parking Map / Dashboard / Events / Notification 갱신
```

---

## 12. 운영 코드와 테스트 코드의 경계

### 12.1 운영 코드에 남긴 것

- 실제 상태 모델
- API 연결과 재시도
- 공용 상태 적용 함수
- 정규화 문자열 parser
- event signal
- Debug UI가 호출할 수 있는 SimulationService

### 12.2 `tests/`에만 둔 것

- 테스트 프로세스의 `main()`
- 성공/실패 return code
- 기대 slot 개수
- 기대 상태 비교
- 기대 이벤트 개수
- 회귀 검증 순서

### 12.3 Simulation과 자동 Test의 차이

| 구분 | Simulation | Test |
|---|---|---|
| 목적 | 서버 없이 화면 동작 시연 | 코드 결과 자동 검증 |
| 위치 | `src/simulation/` | `tests/` |
| 앱에 포함 | 현재 포함 | 포함되지 않음 |
| 사용자 실행 | DebugPage 버튼 | CTest |
| 실패 판정 | 없음 | process exit code |
| 상태 값 | `ParkingMockData` | 실제 모듈 결과를 검사 |

`src/simulation/`은 자동 테스트 harness가 아니다. 현재 앱의 Debug/Demo 기능 모듈이다.

---

## 13. 새 자동 테스트 상세

### 13.1 파일

```text
tests/parkingsimulationservice_test.cpp
```

### 13.2 구성

테스트는 다음 실제 소스를 함께 compile한다.

- `ParkingController`
- `ParkingViewState`
- `ParkingMockData`
- `ParkingSimulationService`
- `ApiClient`
- `ParkingResponseParser`
- `ImageLoader`

Mock Controller 복사본을 사용하지 않는다.

### 13.3 검증 순서

1. 임시 config 경로로 실제 Controller 생성
2. 실제 SimulationService 생성
3. `eventLogged` 횟수 수집
4. `seedInitialState()` 실행
5. EV 16개, P 16개 확인
6. EV-01 `NonEvAlert` 확인
7. EV-02 `OvertimeAlert` 확인
8. P-01 `Occupied` 확인
9. P-02 `Vacant` 확인
10. 초기 이벤트 5개 확인
11. EV-03 두 번 전환
12. EV-03 `Occupied`, 번호판 `56C9012` 확인
13. P-03 sensor error 실행과 확인
14. 수동 `PARKING_SLOT,P04,OCCUPIED` 적용 확인
15. 샘플 메시지 일괄 실행
16. EV-01, EV-02, P-03 결과 재확인

### 13.4 CMake 등록

CTest 이름:

```text
parking_simulation_service
```

실행 파일 target:

```text
parkingsimulationservice_test
```

Windows MinGW의 AutoMoc compiler predefines 문제를 피하기 위해 다음 속성을 사용한다.

```cmake
set_target_properties(parkingsimulationservice_test PROPERTIES
    AUTOMOC_COMPILER_PREDEFINES OFF
)
```

---

## 14. 빌드 시스템 변경

메인 앱 target에 다음 파일을 추가했다.

```cmake
src/simulation/parkingmockdata.cpp
src/simulation/parkingmockdata.h
src/simulation/parkingsimulationservice.cpp
src/simulation/parkingsimulationservice.h
```

`BUILD_TESTING` 블록에는 새 test executable과 CTest를 추가했다.

이번 변경은 새 외부 라이브러리를 추가하지 않는다.

사용하는 기존 Qt 모듈:

- Qt Core
- Qt Widgets
- Qt Network

---

## 15. 검증 결과

### 15.1 빌드

실행 명령:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
```

결과:

```text
smart_parking_qt_client.exe compile/link 성공
parkingcontroller_retry_test.exe compile/link 성공
parkingsimulationservice_test.exe compile/link 성공
parkingmappage_add_slot_test.exe compile/link 성공
```

### 15.2 CTest

실행 명령:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

결과:

```text
parkingcontroller_retry .......... Passed
parking_simulation_service ....... Passed
parkingmappage_add_slot .......... Passed
100% tests passed, 0 failed out of 3
```

### 15.3 정적 확인

다음 항목도 확인했다.

- Controller에 Mock 번호판 문자열 없음
- Controller에 `QRandomGenerator` 없음
- Controller에 `m_mockStep` 없음
- Controller에 과거 Mock 함수 선언·정의 없음
- 새 문서 UTF-8 해석 성공
- Markdown code fence 짝수 확인
- 변경 소스 trailing whitespace 없음
- `git diff --check` 오류 없음

---

## 16. 사용자에게 보이는 동작 변화

기존 Demo 동작은 유지했다.

- 앱 시작 시 EV/P 32개 표시
- EV-01 비전기차 경고
- EV-02 장시간 점유 경고
- Debug EV 전환
- Debug sensor 오류
- 일반 주차면 randomize
- 샘플 수신 메시지 실행
- API 연결 재시도

변경된 사용자 표시 문구:

```text
System ready | Mock data displayed
    ↓
System ready | Waiting for parking state
```

API JSON 파싱 오류 문구도 `Mock data displayed` 대신 `Previous state retained`로 일반화했다.

---

## 17. 현재 제한사항

### 17.1 Simulation은 아직 앱 target에 포함된다

Mock와 운영 Controller는 파일과 객체 수준에서 분리됐지만, 현재 `MainWindow`는 항상 `seedInitialState()`를 호출한다.

이유:

- Raspberry Pi 서버가 아직 완료되지 않음
- 기존 시연과 UI 확인 기능 유지 필요
- DebugPage 기능 유지 필요

즉, 구조적 분리는 완료됐지만 Release 실행에서 Demo 데이터가 자동으로 꺼지는 단계까지는 진행하지 않았다.

### 17.2 실제 Pi 입력 Adapter가 없다

현재 실제 연동은 REST snapshot 프로토타입이다.

미구현 항목:

- 최종 TLS/TCP 상태 stream
- 정규화 event receiver
- 실제 `server slot_id ↔ Qt zoneId` mapping
- Raspberry Pi E2E 검증

### 17.3 `processIncomingMessage()`는 실제 socket과 미연결

parser 기능은 있지만 현재 호출자는 SimulationService다. 실제 receiver가 준비되면 Adapter가 호출하도록 연결해야 한다.

### 17.4 snapshot 성공 후 polling이 없다

앱 시작, 주소 변경, 수동 reconnect, 실패 재시도 때만 snapshot을 요청한다.

---

## 18. 향후 실제 Pi 연동 구조

권장 확장 구조:

```text
개발·시연 입력
ParkingMockData
  └─ ParkingSimulationService
           │
           ├──────────────────┐
           ↓                  │
실제 서버 입력                │
Raspberry Pi Server           │
  └─ PiServerStateAdapter     │
           │                  │
           └──────────┬───────┘
                      ↓
              ParkingController
                ├─ ParkingViewState
                └─ Qt UI signals
```

### 18.1 먼저 확정할 계약

1. `server slot_id ↔ Qt zoneId` mapping
2. 점유 상태 enum과 Unknown 처리
3. 경고 event schema
4. ACK/CLEAR 의미
5. timestamp 기준과 시간대
6. snapshot과 실시간 event 충돌 시 우선순위
7. reconnect 후 상태 재동기화 방식
8. Demo 활성화 설정

### 18.2 권장 Adapter 책임

```text
PiServerStateAdapter
  ├─ TLS/TCP 연결
  ├─ framing
  ├─ raw payload 검증
  ├─ server slot ID 변환
  ├─ 정규화 상태/event 생성
  └─ ParkingController 공용 함수 호출
```

### 18.3 Controller에 추가하지 말아야 할 것

- raw socket read loop
- Mock 번호판
- Demo randomize 정책
- server별 raw JSON key 분기
- UI widget 직접 조작
- 테스트 성공/실패 조건

---

## 19. 다음 수정 시 파일 선택 기준

| 수정하려는 내용 | 수정 위치 |
|---|---|
| 초기 Demo 점유 상태 | `parkingmockdata.cpp::initialViewState()` |
| 초기 Demo 이벤트 | `parkingmockdata.cpp::initialEvents()` |
| 샘플 수신 문자열 | `parkingmockdata.cpp::sampleIncomingMessages()` |
| Debug 버튼 시나리오 | `parkingsimulationservice.cpp` |
| 실제 API 주소·재연결 | `parkingcontroller.cpp` |
| 공용 상태 반영 | `ParkingController::apply...Update()` |
| 상태→시각 상태 변환 | `parkingstate.cpp::deriveSlotVisualState()` |
| Debug signal 연결 | `MainWindow::connectPages()` |
| 자동 회귀 기대값 | `tests/parkingsimulationservice_test.cpp` |
| test target | `CMakeLists.txt`의 `BUILD_TESTING` 블록 |
| Pi 실제 입력 | 향후 `PiServerStateAdapter` |

---

## 20. 코드 리뷰 질문에 대한 답변

### 질문

> 테스트 기능 이름을 Controller로 한 것인가, 아니면 Controller 기능을 실제로 작성하고 테스트 데이터를 대입한 것인가?

### 변경 후 정확한 답변

`ParkingController`는 실제 애플리케이션 Controller다. API 연결, 상태 소유, 재연결, 이벤트 signal과 공용 상태 적용을 담당한다.

Mock 데이터와 Debug 동작은 Controller가 소유하지 않는다.

- Mock 값: `ParkingMockData`
- Mock 동작: `ParkingSimulationService`
- 실제 상태 적용: `ParkingController`
- 자동 검증: `tests/parkingsimulationservice_test.cpp`

현재 Pi 서버가 완료되지 않았기 때문에 `MainWindow`가 SimulationService를 함께 생성하여 실제 Controller의 공용 입력 함수에 Demo 데이터를 넣는다.

따라서 “테스트용 Controller”가 아니라 “실제 Controller에 분리된 Simulation 입력을 연결한 개발·시연 구조”다.

---

## 21. 완료 기준 체크리스트

- [x] Mock 초기 데이터가 Controller 밖으로 이동
- [x] Mock 번호판과 경고가 별도 데이터 모듈로 이동
- [x] Debug 동작이 별도 서비스로 이동
- [x] `m_mockStep`이 SimulationService로 이동
- [x] randomize 정책이 SimulationService로 이동
- [x] 샘플 메시지 목록이 MockData로 이동
- [x] Controller의 `start()`가 운영 초기화만 수행
- [x] Controller에 공용 상태 적용 함수 추가
- [x] 상태 시각 변환을 model로 이동
- [x] MainWindow에서 객체 경계를 명시적으로 조립
- [x] 자동 테스트 코드는 `tests/`에 작성
- [x] 새 CTest 추가
- [x] Qt 전체 build 성공
- [x] CTest 3/3 PASS
- [x] 상세 안내서와 작업일지 갱신

---

## 22. 최종 요약

이번 리팩터링은 기존 기능을 삭제하거나 Controller를 가짜 객체로 교체한 작업이 아니다.

기존의 실제 Controller에서 시연 전용 값과 동작을 꺼내 다음 구조로 정리한 작업이다.

```text
ParkingMockData
    → Demo 값

ParkingSimulationService
    → Demo 동작

ParkingController
    → 실제 상태/API/이벤트 조정

tests
    → 자동 검증
```

이제 팀원이 Mock 값을 수정할 때 운영 API 코드를 건드릴 필요가 없고, 실제 Pi 입력을 연결할 때도 SimulationService와 나란히 Adapter를 추가할 수 있다.

현재 남은 핵심 작업은 Mock 분리가 아니라 실제 Pi schema와 ID mapping을 확정하고 `PiServerStateAdapter`를 연결하는 것이다.
