# 2026-07-22 Parking Map 저장 알림·종료 보호 작업일지

> 작업 저장소: `Qt_Client`
> 작업 브랜치: `taejun/rtsp-performance-reconnect`
> 작업 범위: Parking Map 레이아웃 저장 결과 표시, 변경 상태 추적, 종료 보호, 회귀 테스트

---

## 1. 작업 목적

기존 Parking Map 편집기는 화면에 `Unsaved changes`라는 문자열만 표시했다. 실제 변경 여부를 나타내는 상태값은 없었기 때문에 다음 문제가 있었다.

- 창을 닫아도 저장되지 않은 레이아웃을 감지할 수 없었다.
- 종료 전에 저장할지 묻는 절차가 없었다.
- 저장 성공과 실패가 Parking Map 내부 메시지에만 표시됐다.
- 저장 실패를 전체 이벤트·알림 체계에서 확인할 수 없었다.
- 저장, 다시 불러오기, 저장 실패에 따른 상태 변화를 자동 테스트하지 않았다.

이번 작업은 레이아웃 변경 여부를 실제 상태로 관리하고, 사용자가 편집 결과를 잃지 않도록 종료 흐름을 보호하는 데 목적이 있다.

---

## 2. 변경된 파일

```text
src/pages/parkingmappage.h
src/pages/parkingmappage.cpp
  └─ 레이아웃 dirty 상태, 저장 API, 저장 결과 signal

src/mainwindow.h
src/mainwindow.cpp
  └─ 저장 결과 배너·이벤트 연결, 종료 시 Save/Discard/Cancel

src/services/notificationcenter.cpp
  └─ 레이아웃 저장 실패를 WARNING 알림으로 분류

tests/parkingmappage_add_slot_test.cpp
  └─ 저장·reload·저장 실패 회귀 테스트
```

---

## 3. 소프트웨어 구조

```text
[사용자 레이아웃 편집]
        │
        ├─ Add / Delete
        ├─ Move / Resize / Rotate
        ├─ 속성 편집
        ├─ 표준 크기 적용
        └─ Reset default
        │
        ▼
ParkingMapPage
  ├─ m_layoutDirty = true
  ├─ 상태 문구 = Unsaved changes
  └─ layoutDirtyChanged(true)
        │
        ├─────────────── Save ───────────────┐
        │                                    │
        ▼                                    ▼
saveParkingZoneLayout()                MainWindow 종료 요청
        │                                    │
        ├─ 성공                              ├─ Save
        │   ├─ dirty = false                 ├─ Discard
        │   └─ layoutSaveResult(true, ...)   └─ Cancel
        │
        └─ 실패
            ├─ dirty = true 유지
            └─ layoutSaveResult(false, ...)
                         │
                         ▼
                     MainWindow
                       ├─ 상단 배너
                       ├─ Events 화면
                       └─ NotificationCenter
```

역할 경계는 다음과 같다.

| 모듈 | 책임 |
|---|---|
| `ParkingMapPage` | 편집 내용과 dirty 상태 소유, 레이아웃 저장 시도, 결과 signal 발생 |
| 레이아웃 모델 저장 함수 | JSON 직렬화와 파일 쓰기, 오류 메시지 반환 |
| `MainWindow` | 앱 종료 정책 결정, 전역 배너와 이벤트 기록 연결 |
| `ParkingController::recordEvent()` | 저장 결과를 기존 이벤트 전달 경로로 보냄 |
| `NotificationCenter` | 실패 이벤트를 알림으로 분류하고 bell badge에 반영 |

---

## 4. 레이아웃 변경 상태 관리

### 4.1 새 상태값

`ParkingMapPage`에 다음 상태가 추가됐다.

```cpp
bool m_layoutDirty = false;
```

외부에서는 다음 함수로 저장되지 않은 변경 여부를 확인한다.

```cpp
bool hasUnsavedLayoutChanges() const;
```

상태 갱신은 `markLayoutDirty()`와 `setLayoutDirty()`를 통해 한 곳에서 수행한다. 같은 상태가 반복해서 설정될 때 `layoutDirtyChanged` signal이 불필요하게 반복 발생하지 않는다.

### 4.2 dirty가 true가 되는 동작

```text
- Add General Slot
- Add EV Slot
- Delete
- drag 이동
- resize 또는 rotation 결과 반영
- zone ID, type, display name, channel, IVA, sensor 등 속성 변경
- Reset to default shape
- Reset default
```

실제 값이 바뀌지 않은 편집이나 이동은 dirty 상태로 만들지 않는다. 이를 위해 변경 전·후 `ParkingZoneLayout`의 모든 필드를 비교한다.

### 4.3 dirty가 false가 되는 동작

```text
- 앱 시작 시 로컬 레이아웃 또는 기본 레이아웃 로드
- Save 성공
- Reload 성공
```

Save가 실패하면 파일에 반영되지 않았으므로 dirty는 true로 유지한다.

---

## 5. 저장 처리

### 5.1 공개 저장 경계

기존 버튼 전용 private slot과 별도로 다음 공개 함수가 추가됐다.

```cpp
bool saveLayoutNow(QString *errorMessage = nullptr);
```

이 함수는 Save 버튼과 `MainWindow::closeEvent()`가 같은 저장 로직을 재사용하게 한다.

### 5.2 저장 성공

```text
1. 그래픽 item 위치와 크기를 layout model에 동기화
2. JSON 파일 저장
3. dirty = false
4. Parking Map 상태 = Saved local layout
5. layoutSaveResult(true, 저장 경로 포함 메시지) 발생
6. MainWindow 상단 배너를 녹색 성공 상태로 변경
7. LAYOUT_SAVED / DONE 이벤트 기록
```

### 5.3 저장 실패

```text
1. JSON 파일 저장 실패
2. 오류 문자열을 호출자에게 반환
3. dirty = true 유지
4. Parking Map 상태 = Save failed
5. layoutSaveResult(false, 오류 원인) 발생
6. MainWindow 상단 배너를 붉은 실패 상태로 변경
7. LAYOUT_SAVE_FAILED / FAILED 이벤트 기록
8. NotificationCenter가 실패 알림을 bell badge에 반영
```

Save 버튼으로 발생한 실패는 Parking Map 경고창에도 표시된다.

---

## 6. 앱 종료 보호

`MainWindow::closeEvent()`가 Parking Map의 dirty 상태를 확인한다.

```text
창 닫기 요청
  │
  ├─ dirty = false
  │    └─ 즉시 정상 종료
  │
  └─ dirty = true
       └─ 확인창 표시
            ├─ Save
            │    ├─ 성공 → 종료
            │    └─ 실패 → 오류 표시 + 종료 취소
            ├─ Discard
            │    └─ 저장하지 않고 종료
            └─ Cancel
                 └─ 종료 취소 + 편집 화면 유지
```

기본 선택은 `Save`다. 사용자가 실수로 Enter를 눌러도 저장을 우선하도록 했다.

---

## 7. 이벤트와 알림

저장 결과는 기존 Controller 이벤트 경로를 재사용한다.

| 결과 | source | event type | status | 알림 |
|---|---|---|---|---|
| 성공 | `PARKING_MAP` | `LAYOUT_SAVED` | `DONE` | 이벤트만 기록 |
| 실패 | `PARKING_MAP` | `LAYOUT_SAVE_FAILED` | `FAILED` | 이벤트 기록 + bell 알림 |

`DONE` 상태는 NotificationCenter의 일반 성공 이벤트 필터에 따라 bell 알림을 만들지 않는다. 실패 이벤트는 이름에 `FAILED`가 포함되고 상태도 `FAILED`이므로 경고성 알림으로 분류된다.

---

## 8. 자동 테스트

기존 `parkingmappage_add_slot_test`에 다음 검증을 추가했다.

```text
1. 최초 로드 직후 dirty = false
2. 슬롯 추가 후 dirty = true
3. 반복 Add/Delete 후 레이아웃 개수 유지
4. 정상 저장 성공
5. 정상 저장 후 dirty = false
6. 다시 슬롯 추가 후 dirty = true
7. Reload 후 dirty = false
8. Reload 후 저장된 row 수 복원
9. 디렉터리를 파일 경로로 사용해 저장 실패 유도
10. 실패 오류 문자열 존재
11. 저장 실패 후 dirty = true 유지
```

저장 실패 테스트는 실제 사용자 레이아웃 파일을 건드리지 않는다. `QTemporaryDir` 아래에 파일 대신 디렉터리를 만들고 그 경로에 저장을 시도해 쓰기 실패를 재현한다.

---

## 9. 검증 결과

### 9.1 빌드

```text
[BUILD OK]
smart_parking_qt_client.exe 컴파일 및 링크 성공
parkingmappage_add_slot_test.exe 컴파일 및 링크 성공
```

### 9.2 CTest

```text
parkingcontroller_retry .......... Passed
parking_simulation_service ....... Passed
parkingmappage_add_slot .......... Passed

100% tests passed, 0 tests failed out of 3
```

### 9.3 정적 확인

```text
git diff --check 통과
Python 사용 없음
로컬 레이아웃 및 credential 파일 변경 없음
```

---

## 10. 수동 QA 항목

자동 테스트는 저장 상태와 파일 결과를 검증한다. 실제 창에서 다음 UI 흐름은 최종 확인 대상으로 남긴다.

- 레이아웃 수정 후 창 닫기 → Save → 저장 후 종료
- 레이아웃 수정 후 창 닫기 → Discard → 저장하지 않고 종료
- 레이아웃 수정 후 창 닫기 → Cancel → 창 유지
- 쓰기 불가능한 경로에서 Save → 오류 표시 후 창 유지
- Save 성공 시 녹색 배너와 Events 기록 확인
- Save 실패 시 붉은 배너와 bell badge 확인

---

## 11. 최종 결과

Parking Map의 `Unsaved changes`는 더 이상 단순 문구가 아니라 실제 상태가 됐다. 레이아웃 변경, 저장, reload, 저장 실패가 일관된 상태 전이를 사용하며, 앱 종료 시 사용자가 저장 여부를 명확히 선택할 수 있다.

저장 실패는 조용히 넘어가지 않고 편집 상태를 보존한 채 오류, 이벤트, 알림으로 전달된다. 따라서 레이아웃 유실 가능성을 줄였고 이후 서버 zone mapping이나 실제 주차장 배치 작업을 더 안전하게 진행할 수 있다.

---

## 12. 후속 UX 정리: Edit zone 제거

슬롯 우클릭 메뉴의 `Edit zone`은 별도 창을 열거나 값을 변경하는 기능이 아니었다. 선택한 슬롯에 대해 상단 `Edit layout`을 활성화하고 오른쪽 상세 패널을 새로 고치는 동작만 수행해, 이미 편집 모드일 때는 아무 변화도 보이지 않았다.

기능 중복과 오해를 없애기 위해 `Edit zone`을 제거했다.

```text
편집 시작
  └─ 상단 Edit layout

슬롯 우클릭
  ├─ Reset to default shape
  └─ Delete zone
```

`Reset to default shape`는 슬롯의 위치, zone ID, type, channel, IVA, sensor mapping을 유지한다. 사용자가 조절한 모양만 기본 크기 `84 × 58`과 회전 `0°`로 복원한다.

자동 테스트에서는 선택 슬롯을 `132 × 96`, `35°`로 변경해 저장한 뒤 reset을 호출하고, 슬라이더와 모델이 `84 × 58`, `0°`로 돌아오는지 확인한다.

실제 앱 수동 QA에서는 `EV-01`을 너비 `250px`, 회전 `96°`로 변형한 뒤 우클릭 `Reset to default shape`를 실행했다. 화면과 상세 패널 모두 `84 × 58`, `0°`로 복원됐고 `CH1`, `IVA1`, `HALL_EV_01`, x/y 위치는 유지됐다. 테스트 후 `Reload`를 실행해 임시 dirty 상태를 제거하고 저장된 로컬 레이아웃으로 복귀했다.

```text
[MANUAL PASS] 250 × 58, 96° → 84 × 58, 0°
[PRESERVED] position / CH1 / IVA1 / HALL_EV_01
[CLEANUP PASS] Reload → Loaded local layout
```

### 회전 값과 맵 표시 불일치 수정

추가 수동 확인 중 `EV-04`가 맵에서는 회전돼 있지만 상세 패널에는 `0°`로 표시되는 문제를 재현했다.

```text
회전 handle drag
  └─ QGraphicsItem rotation 변경
       └─ drag finished
            └─ 위치와 크기만 ParkingZoneLayout에 복사
                 └─ rotation 복사 누락

결과
  ├─ 그래픽: 회전 상태
  ├─ 모델/슬라이더: 0°
  └─ Reset: 이미 기본 상태라고 잘못 판단하고 return
```

수정 내용:

- drag 종료 시 `item->rotation()`을 `ParkingZoneLayout::rotation`에 동기화
- Reset의 기본 상태 판정에서 모델뿐 아니라 실제 graphics item의 width, height, rotation도 확인
- 모델 값이 `0°`로 잘못 남아 있어도 실제 item이 회전돼 있으면 반드시 `setRotation(0)` 실행
- 위치는 실제 item 위치를 기준으로 보존

회귀 테스트에는 다음 두 경로를 추가했다.

```text
1. graphics item 73° → drag-finished 동기화 → 상세 rotation 73°
2. model 0° / graphics item 47° 불일치 → Reset → graphics item 0°
```

별도 `parkingmappage_add_slot` 테스트 실행 결과는 PASS다. 실행 중인 실제 앱에는 사용자의 미저장 `EV-04` 변경이 남아 있어, 저장 여부를 결정하고 앱을 종료하기 전까지 전체 실행 파일 재링크는 보류한다.

### 실행 중 발견한 종료 관련 관찰

수동 확인 창을 닫은 뒤 `smart_parking_qt_client` 프로세스가 창 제목 없이 잠시 남아 전체 실행 파일 링크 전에 종료 처리가 필요했다. 실행 UI는 이미 닫혀 있었으며 기존 프로세스 종료 후 빌드는 정상 통과했다.

```text
증상: 창은 없지만 smart_parking_qt_client 프로세스 잔류
영향: smart_parking_qt_client.exe 재링크 가능성 저하
이번 처리: 잔류 프로세스 종료 후 재빌드
후속 확인: RTSP worker 또는 network 종료 대기 시간이 정상 범위인지 별도 점검
```
