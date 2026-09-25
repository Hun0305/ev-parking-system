# 2026-07-23 Qt 이벤트 계약 및 입력 Adapter 리팩토링 작업일지

## 1. 작업 목적

Qt Client가 외부 문자열 메시지 형식에 직접 의존하던 범위를 줄이고, 이후 Raspberry Pi 서버의 정규화 이벤트 schema와 연결할 수 있는 명시적인 내부 계약을 마련했다.

이번 작업은 다음 범위로 제한했다.

- 이벤트 회귀 테스트를 먼저 추가한다.
- UI와 서비스 사이의 이벤트 전달을 `MonitoringEvent` 값 객체로 통일한다.
- 외부 문자열 파싱을 `IncomingMessageAdapter`로 분리한다.
- Parking Map 상태 변경 정책과 알림 정책은 기존 Controller/Service에 유지한다.
- Parking Map 편집 동작과 RTSP 동작은 변경하지 않는다.

## 2. 작업 브랜치와 기준

```text
base: develop / 400728f
branch: taejun/qt-client-event-refactor
```

기능과 문서를 한 번에 묶지 않고 다음 의미 단위로 커밋을 분리했다.

```text
69204d5 test: add parking event flow regression coverage
d7a2d29 refactor(core): introduce typed monitoring event contract
c42c0f6 refactor(adapter): extract incoming message parser
```

## 3. 변경 전후 흐름

변경 전:

```text
raw QString
  -> ParkingController parsing
  -> 여러 QString 인자의 eventLogged signal
  -> MainWindow / Dashboard / Events / Notification / Diagnostics
```

변경 후:

```text
raw QString
  -> IncomingMessageAdapter
  -> ParsedIncomingMessage
  -> ParkingController의 상태 변경 및 정책 적용
  -> MonitoringEvent
  -> MainWindow / Dashboard / Events / Notification / Diagnostics
```

`IncomingMessageAdapter`는 외부 형식을 해석하고 정규화하는 역할만 수행한다. 슬롯 상태 변경, 경고 활성화, 알림 여부 같은 정책은 소유하지 않는다.

## 4. MonitoringEvent 내부 계약

추가 파일:

```text
src/models/monitoringevent.h
src/models/monitoringevent.cpp
```

현재 필드:

```text
id
occurredAt
sourceId
eventType
severity
ackState
message
status
```

설계 기준:

- `eventType`은 문자열로 유지해 새 서버 이벤트가 추가될 때마다 enum과 전체 UI를 동시에 수정하지 않아도 된다.
- `severity`와 `ackState`는 클라이언트 내부에서 의미가 고정된 enum으로 제공한다.
- 아직 알 수 없는 외부 상태를 잃지 않도록 원본 정규화 상태인 `status`도 보존한다.
- signal은 여러 문자열 인자 대신 `const MonitoringEvent&` 하나를 전달한다.
- `Q_DECLARE_METATYPE(MonitoringEvent)`로 Qt signal/slot 전달 가능성을 명시했다.

## 5. IncomingMessageAdapter 경계

추가 파일:

```text
src/adapters/incomingmessageadapter.h
src/adapters/incomingmessageadapter.cpp
```

현재 분류:

```text
Invalid
NormalizedEvent
ParkingSlot
HallSensor
EvAlert
FireAlarm
Unsupported
```

Adapter 결과에는 원문, 메시지 유형, source ID, value, event type, status, message, 오류 설명이 포함된다. Controller는 이 결과를 받아 기존 상태 모델과 알림 정책에 반영한다.

## 6. 회귀 테스트

추가 테스트:

```text
tests/parkingeventflow_test.cpp
tests/incomingmessageadapter_test.cpp
```

주요 검증 항목:

- 잘못된 입력이 기존 상태를 오염시키지 않는지 확인
- 주차 슬롯 메시지가 점유 상태를 갱신하는지 확인
- 정규화 이벤트의 message에 쉼표가 있어도 내용이 보존되는지 확인
- Hall sensor clear가 기존 경고를 해제하는지 확인
- 화재 alarm/ack 흐름이 알림 상태에 반영되는지 확인
- Adapter가 지원 형식을 올바르게 분류하고 필드를 추출하는지 확인
- 미지원 형식과 필수 필드 누락을 구분하는지 확인

2026-07-23 최종 검증:

```text
[BUILD OK] smart_parking_qt_client.exe compile/link 성공
[PASS] CTest 7/7, 0 failed
```

전체 CTest 목록:

```text
parkingcontroller_retry
parking_simulation_service
parking_event_flow
incoming_message_adapter
diagnostics_service
camera_settings
parkingmappage_add_slot
```

## 7. 빌드 환경 참고

일반 PowerShell에서 MinGW `bin` 경로가 `PATH`에 없으면 Qt AutoMoc가 `cc1plus`를 실행할 때 `0xC0000135`로 종료될 수 있다. 이번 최종 빌드는 명령 범위에서만 다음 경로를 `PATH` 앞에 추가해 검증했다.

```powershell
$env:Path = 'C:\Qt\Tools\mingw1310_64\bin;' + $env:Path
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
```

테스트 실행 시에는 Qt runtime 경로도 함께 사용했다.

```powershell
$env:Path = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;' + $env:Path
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

## 8. 후속 작업 후보

다음 단계는 이번 경계를 유지하면서 `ParkingController`의 책임을 실제 변경 필요에 따라 점진적으로 줄이는 것이다.

우선 검토 후보:

1. 슬롯 상태 보관과 조회를 `ParkingStateStore` 성격의 모듈로 분리
2. API 요청, retry, runtime data source 연결을 `ParkingApiSession` 성격의 모듈로 분리
3. Raspberry Pi의 정규화 event schema와 `MonitoringEvent` 변환 규칙 확정
4. `server slot_id`와 Qt `zoneId`의 명시적 mapping schema 정의

모든 클래스를 선제적으로 interface/factory로 감싸지 않는다. 실제 교체 가능성, 동시성 경계, 독립 테스트 가치가 확인되는 책임부터 분리한다.
