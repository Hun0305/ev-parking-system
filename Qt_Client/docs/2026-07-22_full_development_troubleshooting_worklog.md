# 2026-07-22 Qt Client 전체 개발·Troubleshooting 일지

> 작업일: 2026-07-22
> 저장소: `Qt_Client`
> 작업 브랜치: `taejun/rtsp-performance-reconnect`
> PR: `VEDA-4th-Team5/Qt_Client` PR #1
> 최종 반영 커밋: `942463a refactor: separate parking mock data and simulation`
> 문서 범위: 코드 분석, Mock/Controller 모듈화, 테스트, 실행 확인, 문서화, GitHub 설정과 PR 갱신, 발생 문제와 해결 과정

---

## 1. 오늘 작업 요약

오늘 작업의 핵심은 `ParkingController`에 섞여 있던 Mock 데이터와 Debug 시뮬레이션 동작을 실제 운영 기능에서 분리하는 것이었다.

팀 리뷰에서 다음 취지의 요청이 전달됐다.

```text
현재 controller.cpp에 임의 테스트 값들이 들어가 있는데,
기존 Controller 역할과 테스트 코드를 확인하고 테스트 코드를 분리해 달라.
```

코드를 확인한 결과 `ParkingController`는 이름만 Controller인 테스트 클래스가 아니었다. 실제 앱에서 다음 기능을 담당하는 운영 Controller였다.

- Raspberry Pi REST API 설정
- HTTP/HTTPS JSON 요청
- API 재연결과 backoff
- 서버 snapshot 상태 적용
- 주차 상태 보관
- 이벤트와 UI signal 전달
- 정규화 문자열 메시지 파싱

다만 같은 파일에 다음 Demo/Mock 책임도 들어 있었다.

- 초기 EV/P 32개 상태
- 테스트 번호판
- 비전기차·장시간 점유·sensor 오류 시뮬레이션
- EV-03 상태 전환 단계
- 일반 주차면 무작위 상태
- 샘플 수신 문자열

오늘 작업에서는 기능을 삭제하지 않고 다음 구조로 분리했다.

```text
ParkingMockData
  └─ Mock 값과 샘플 데이터

ParkingSimulationService
  └─ Mock/Debug 동작

ParkingController
  └─ 실제 상태, API, 이벤트 조정

tests
  └─ 자동 검증 코드
```

최종 결과:

- Qt 전체 빌드 성공
- CTest 3/3 통과
- `git diff --cached --check` 통과
- 커밋과 원격 push 완료
- 기존 PR #1 자동 갱신
- 상세 설명서와 Troubleshooting 기록 작성

---

## 2. 작업 시작 시 기준 상태

### 2.1 저장소

```text
Workspace:
C:\Users\5-15\Documents\VEDA_FINAL

실제 Git 저장소:
C:\Users\5-15\Documents\VEDA_FINAL\Qt_Client
```

Git 명령은 반드시 `Qt_Client`에서 실행했다.

### 2.2 브랜치와 PR

```text
branch: taejun/rtsp-performance-reconnect
remote: origin
repository: VEDA-4th-Team5/Qt_Client
PR: #1
base: develop
head: taejun/rtsp-performance-reconnect
```

기존 PR이 열려 있었으므로 새 PR을 만들지 않고 동일한 head branch에 커밋을 push하는 방식을 사용했다.

### 2.3 Raspberry Pi 연결 상태

Raspberry Pi 서버가 아직 완료되지 않아 실제 Qt↔Pi End-to-End 연결을 검증할 수 없는 상태였다.

따라서 오늘 변경에서는 다음 원칙을 적용했다.

```text
기존 Demo 동작 유지
  +
Mock 책임을 운영 Controller 밖으로 분리
  +
향후 실제 Pi Adapter 연결 지점 확보
```

---

## 3. 오늘 수행한 작업 순서

### 3.1 실제 Controller 여부 확인

`src/controllers/parkingcontroller.h/.cpp`를 함수 단위로 확인했다.

확인 결과:

```text
ParkingController
  ├─ 실제 API client 생성
  ├─ 설정 파일 로드
  ├─ GET 요청
  ├─ JSON snapshot 적용
  ├─ 재연결 timer
  ├─ 상태 저장
  ├─ UI signal
  └─ 이벤트 기록
```

즉, Controller 기능을 흉내 낸 테스트 클래스가 아니라 실제 앱 Controller였다.

문제는 Mock 초기화와 Debug 기능이 같은 클래스에 포함돼 있다는 점이었다.

### 3.2 Mock 값과 동작 분리 설계

Mock을 다음 두 책임으로 다시 나눴다.

```text
데이터 값
  → ParkingMockData

동작 순서
  → ParkingSimulationService
```

Controller는 데이터 출처가 Mock인지 서버인지 구분하지 않고 전달받은 상태만 적용하도록 했다.

### 3.3 도메인 시각 상태 변환 이동

기존 Controller 내부 helper였던 상태→시각 상태 변환을 모델로 이동했다.

```cpp
SlotVisualState deriveSlotVisualState(
    SlotState state,
    bool vehicleTypeKnown,
    bool isEv,
    const QString &alarmText = QString());
```

이동 이유:

- 서버 snapshot과 Mock 데이터가 같은 변환 규칙을 사용해야 함
- MockData가 Controller 내부 helper에 의존하면 안 됨
- 상태 의미는 Controller보다 model 책임에 가까움

### 3.4 MainWindow 조립 변경

`MainWindow`가 운영 Controller와 SimulationService를 명시적으로 생성하도록 변경했다.

```text
buildUi()
  ↓
ParkingController 생성
  ↓
ParkingSimulationService 생성
  ↓
connectPages()
  ↓
seedInitialState()
  ↓
ParkingController::start()
```

### 3.5 새 자동 테스트 추가

`tests/parkingsimulationservice_test.cpp`를 추가했다.

검증 항목:

- EV 16개 생성
- General 16개 생성
- EV-01 비전기차 경고
- EV-02 장시간 점유 경고
- 초기 이벤트 5개
- EV-03 상태 전환
- P-03 sensor 오류
- 수동 문자열 적용
- 샘플 문자열 적용

### 3.6 설명서 작성

오늘 코드 구조를 처음 보는 팀원이 이해할 수 있도록 다음 문서를 작성했다.

```text
docs/2026-07-22_qt_client_module_implementation_guide.md
docs/2026-07-22_parking_simulation_modularization_worklog.md
docs/2026-07-22_parking_mock_simulation_refactoring_details.md
```

문서별 역할:

| 문서 | 역할 |
|---|---|
| `qt_client_module_implementation_guide.md` | Qt Client 전체 모듈과 함수 설명 |
| `parking_simulation_modularization_worklog.md` | 이번 리팩터링 요약 작업일지 |
| `parking_mock_simulation_refactoring_details.md` | 변경 전·후 구조와 함수별 상세 설계 |
| 현재 문서 | 오늘 전체 작업과 Troubleshooting 통합 일지 |

### 3.7 실행 확인

빌드된 Qt 실행 파일을 직접 실행했다.

```text
실행 파일:
build/smart_parking_qt_client.exe
```

실행 직후 프로세스가 정상 응답하는 것을 확인했다.

Controller는 별도 실행 파일이 아니다.

```text
smart_parking_qt_client.exe
  └─ 내부 모듈: ParkingController
```

자동 테스트 실행 파일은 별도로 생성된다.

```text
parkingcontroller_retry_test.exe
parkingsimulationservice_test.exe
parkingmappage_add_slot_test.exe
```

### 3.8 Git commit과 push

변경 파일을 명시적으로 stage하고 다음 검사를 수행했다.

```text
git status -sb
git diff --cached --name-status
git diff --cached --check
```

커밋:

```text
942463a refactor: separate parking mock data and simulation
```

push:

```text
bed00ae..942463a
taejun/rtsp-performance-reconnect
  → origin/taejun/rtsp-performance-reconnect
```

기존 PR #1의 head SHA가 `942463a02182c9263fd49f046eb0d31291be0c42`로 갱신된 것을 확인했다.

---

## 4. 변경 후 소프트웨어 구조

### 4.1 전체 구조

```text
[입력 1: Demo/Debug]
ParkingMockData
  ├─ initialViewState()
  ├─ initialEvents()
  └─ sampleIncomingMessages()
       ↓
ParkingSimulationService
  ├─ seedInitialState()
  ├─ toggleMockEv()
  ├─ triggerNonEvAlert()
  ├─ triggerOvertimeAlert()
  ├─ triggerSensorError()
  ├─ randomizeParkingSlots()
  ├─ runSampleMessages()
  └─ applyManualMessage()
       │
       ├────────────────────────┐
       ↓                        │
[입력 2: 서버 REST prototype]   │
ApiClient                       │
  └─ ParkingResponseParser      │
       │                        │
       └──────────────┬─────────┘
                      ↓
              ParkingController
                ├─ ParkingViewState
                ├─ API 연결과 재시도
                ├─ 상태 적용
                ├─ 이벤트 기록
                └─ UI signal
                      ↓
                 MainWindow
                ├─ ParkingMapPage
                ├─ DashboardPage
                ├─ EventsPage
                └─ NotificationCenter
```

### 4.2 의존성 규칙

```text
ParkingMockData
  → ParkingSimulationService를 알지 않음
  → ParkingController를 알지 않음
  → UI와 Network를 알지 않음

ParkingSimulationService
  → ParkingMockData 사용
  → ParkingController 공용 입력 호출

ParkingController
  → ParkingMockData를 알지 않음
  → ParkingSimulationService를 알지 않음
  → 입력 출처와 관계없이 상태 적용

MainWindow
  → 모든 객체를 생성하고 연결
```

---

## 5. 파일별 변경 내용

### 5.1 새 파일

#### `src/simulation/parkingmockdata.h/.cpp`

Mock 값 전용 모듈이다.

포함 데이터:

- EV-01~EV-16 초기 상태
- P-01~P-16 초기 상태
- Demo 번호판
- 초기 경고
- 초기 이벤트
- 샘플 수신 문자열

#### `src/simulation/parkingsimulationservice.h/.cpp`

Mock/Debug 동작 전용 서비스다.

포함 동작:

- 초기 상태 주입
- EV-03 상태 전환
- 비전기차 경고
- 장시간 점유 경고
- Hall sensor 오류
- 일반 주차면 randomize
- 샘플 문자열 실행
- 수동 문자열 전달

#### `tests/parkingsimulationservice_test.cpp`

실제 `ParkingController`, `ParkingMockData`, `ParkingSimulationService`를 조합하여 경계를 검증한다.

### 5.2 수정 파일

#### `src/controllers/parkingcontroller.h/.cpp`

제거:

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
QRandomGenerator include
```

추가 또는 공개:

```text
replaceViewState()
applyEvSlotUpdate()
applyParkingSlotUpdate()
```

`start()` 변경:

```text
변경 전
initializeMockData()
  ↓
initializeApiClient()

변경 후
initializeApiClient()
```

파싱 오류 문구 변경:

```text
변경 전:
Invalid parking API response | Mock data displayed

변경 후:
Invalid parking API response | Previous state retained
```

#### `src/models/parkingstate.h/.cpp`

`deriveSlotVisualState()`를 추가했다.

#### `src/mainwindow.h/.cpp`

`ParkingSimulationService` 멤버를 추가하고 DebugPage signal을 SimulationService로 연결했다.

예외:

```text
Clear alarms
  → 현재 상태의 공용 ACK 기능
  → ParkingController::clearAlarms() 유지
```

#### `CMakeLists.txt`

- 메인 앱 target에 simulation 소스 추가
- `parkingsimulationservice_test` 실행 파일 추가
- `parking_simulation_service` CTest 추가
- Windows AutoMoc workaround 추가

---

## 6. 현재 앱 시작 순서

오늘 다시 확인한 실제 실행 순서는 다음과 같다.

```text
MainWindow 시작
  ├─ UI 생성
  ├─ ParkingController 생성
  ├─ ParkingSimulationService 생성
  ├─ signal/slot 연결
  ├─ Mock 초기 상태 주입
  └─ 서버 API 연결 시작
```

코드 순서:

```cpp
connectPages();
m_parkingSimulationService->seedInitialState();
m_parkingController->start();
```

즉, 현재는 서버를 먼저 호출하지 않는다.

```text
Mock 먼저 표시
  ↓
서버 API 호출
  ├─ 성공 → 서버 snapshot으로 교체
  └─ 실패 → 직전 Mock 상태 유지 + 재연결
```

이 방식은 Pi 서버가 미완료인 현재 시연을 유지하기 위한 것이다.

향후 권장 동작:

```text
Demo mode = true
  → Mock 먼저 주입

Demo mode = false
  → Mock 없이 WAITING DATA
  → 서버부터 연결
```

현재 `Demo mode` runtime flag는 아직 없다.

---

## 7. 자동 테스트와 검증

### 7.1 빌드 명령

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
```

결과:

```text
smart_parking_qt_client.exe link 성공
parkingcontroller_retry_test.exe link 성공
parkingsimulationservice_test.exe link 성공
parkingmappage_add_slot_test.exe link 성공
```

### 7.2 테스트 명령

```powershell
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

결과:

```text
1/3 parkingcontroller_retry .......... Passed  1.42 sec
2/3 parking_simulation_service ....... Passed  0.20 sec
3/3 parkingmappage_add_slot .......... Passed  0.43 sec

100% tests passed
0 tests failed out of 3
Total Test time: 2.07 sec
```

### 7.3 추가 검증

```text
Controller에 Mock 번호판 없음
Controller에 m_mockStep 없음
Controller에 QRandomGenerator 없음
과거 Mock 함수 선언·정의 없음
문서 UTF-8 해석 성공
문서 code fence 짝수 확인
docs 전체에 렌더러 전용 다이어그램 문법 없음
git diff --cached --check 통과
```

---

## 8. Troubleshooting 상세

## 8.1 Raspberry Pi 서버 미완료 상태

### 증상

Pi 서버가 아직 완료되지 않아 실제 서버 연결 상태와 화면 데이터를 End-to-End로 확인할 수 없었다.

### 위험

- 화면에 데이터가 보이면 서버 연결 성공으로 오해할 수 있음
- Mock 제거 시 UI 개발과 시연이 중단될 수 있음
- Mock 유지 시 운영 Controller가 테스트 코드로 오해될 수 있음

### 해결

```text
Mock 동작은 유지
  +
Mock 데이터와 동작을 Controller 밖으로 이동
  +
서버 성공 시 snapshot으로 교체하는 기존 흐름 유지
```

### 확인 결과

- Pi 미연결 상태에서도 기존 Demo 화면 유지
- Controller는 Mock 값을 알지 않음
- 실제 Pi 연동용 Adapter 추가 가능

### 남은 작업

- Demo mode flag
- Pi server Adapter
- `slot_id ↔ zoneId` mapping
- 실제 E2E 테스트

---

## 8.2 Controller에 Mock 책임 혼합

### 증상

`ParkingController` 안에 실제 API 코드와 Mock 값·동작이 함께 있었다.

### 원인

Pi 서버 없이 UI를 시연하는 기능을 빠르게 구현하면서 Controller에 초기 상태와 Debug 함수를 직접 추가한 프로토타입 구조였다.

### 해결

```text
Mock 값
  → ParkingMockData

Mock 동작
  → ParkingSimulationService

상태 적용
  → ParkingController 공용 함수
```

### 검증

- Controller source에서 Mock 번호판 검색 결과 없음
- Controller source에서 과거 Mock 함수 검색 결과 없음
- simulation service test 통과

### 교훈

Controller는 입력을 조정해야 하지만 입력 예시 값과 시나리오까지 소유하면 책임 경계가 흐려진다.

---

## 8.3 새 CTest target의 Windows AutoMoc 실패

### 증상

새 `parkingsimulationservice_test` target을 처음 빌드할 때 Windows MinGW 환경의 AutoMoc compiler predefines 생성 과정에서 오류가 발생했다.

### 원인

기존 `parkingmappage_add_slot_test`에서도 사용 중인 Windows/MinGW AutoMoc predefines 호환 문제와 같은 유형이었다.

### 해결

```cmake
set_target_properties(parkingsimulationservice_test PROPERTIES
    AUTOMOC_COMPILER_PREDEFINES OFF
)
```

### 검증

- 새 test executable compile/link 성공
- `parking_simulation_service` CTest 통과
- 전체 Qt 앱 link 성공

### 교훈

같은 Qt/MinGW 환경에서 QObject가 포함된 새 독립 테스트 target을 추가할 때 기존 test target의 AutoMoc 설정을 먼저 확인한다.

---

## 8.4 렌더러 전용 다이어그램 사용 문제

### 증상

상세 설계서에 특정 renderer가 필요한 구조도 4개를 사용했으나 팀의 일반 Markdown 문서에서는 사용하지 않는 방식이라는 피드백이 있었다.

### 해결

전용 다이어그램 블록을 모두 제거하고 다음 기호만 사용한 text 구조도로 교체했다.

```text
│
├─
└─
↓
→
```

### 검증

`docs/` 전체에서 전용 다이어그램 시작 태그와 관련 키워드를 검색하고 결과가 없음을 확인했다.

### 교훈

팀 Markdown 문서는 특정 renderer에 의존하지 않는 일반 text 구조도를 사용한다.

---

## 8.5 `git diff --cached --check` 후행 공백 오류

### 증상

커밋 직전 staged diff 검사에서 문서의 Markdown hard line break용 공백 두 칸이 trailing whitespace로 검출됐다.

검출 파일:

```text
parking_mock_simulation_refactoring_details.md
parking_simulation_modularization_worklog.md
qt_client_module_implementation_guide.md
```

### 원인

Markdown에서 강제 줄바꿈을 만들기 위해 문장 끝에 공백 두 칸을 사용했다.

### 해결

- 문장 끝 공백 제거
- 문서를 다시 stage
- `git diff --cached --check` 재실행

### 검증

```text
git diff --cached --check
  → 출력 없음
  → exit code 0
```

### 교훈

이 저장소에서는 Markdown hard break보다 빈 줄이나 자연스러운 문단 구분을 사용한다.

---

## 8.6 GitHub CLI가 설치돼 있지만 `gh`를 찾지 못함

### 증상

```text
gh : 명령을 찾을 수 없음
```

`winget`은 GitHub CLI가 이미 설치됐다고 표시했다.

### 확인

실제 실행 파일이 다음 경로에 존재했다.

```text
C:\Program Files\GitHub CLI\gh.exe
```

GitHub CLI 버전:

```text
gh version 2.96.0
```

### 원인

GitHub CLI 설치 경로가 현재 PowerShell의 PATH에 포함되지 않았다.

### 해결

전체 경로로 실행했다.

```powershell
& 'C:\Program Files\GitHub CLI\gh.exe' auth login
```

현재 shell에서 일반 `gh` 명령을 사용하려면 다음 경로를 PATH에 추가할 수 있다.

```powershell
$env:Path += ';C:\Program Files\GitHub CLI'
```

### 교훈

`winget`이 설치 완료라고 표시해도 `Get-Command gh`와 실제 설치 경로를 별도로 확인한다.

---

## 8.7 GitHub 인증 성공 후 격리 환경에서 invalid 표시

### 증상

사용자 PowerShell에서는 로그인 성공이 표시됐지만 격리된 명령 실행에서는 token invalid로 확인됐다.

### 원인

사용자 Windows keyring에 저장된 GitHub 인증 정보를 격리 환경에서 읽지 못했다.

### 해결

사용자 환경 권한으로 다음을 다시 확인했다.

```powershell
& 'C:\Program Files\GitHub CLI\gh.exe' auth status
```

확인 결과:

```text
Logged in to github.com account taejundev777
Git protocol: HTTPS
Credential source: keyring
```

그 후 같은 사용자 환경 권한으로 `git push`를 수행했다.

### 검증

```text
origin/taejun/rtsp-performance-reconnect
  → 942463a 반영

PR #1 head_sha
  → 942463a02182c9263fd49f046eb0d31291be0c42
```

### 교훈

인증이 keyring에 저장되는 도구는 격리된 shell과 사용자 interactive shell에서 결과가 다를 수 있다.

---

## 8.8 기존 PR을 다시 보내는 방법 혼동

### 질문

```text
PR을 다시 보내려면 push부터 해야 하는가?
```

### 확인 상태

- 현재 브랜치에 열려 있는 PR #1 존재
- 로컬 변경은 처음에는 미커밋 상태
- 원격 branch는 기존 커밋 `bed00ae` 상태

### 올바른 순서

```text
변경 확인
  ↓
git add
  ↓
git commit
  ↓
git push
  ↓
기존 PR 자동 갱신
  ↓
리뷰 댓글 답변
  ↓
Re-request review
```

### 교훈

같은 head branch의 열린 PR이 있으면 새 PR을 만들지 않는다. 새 커밋을 같은 branch에 push하면 기존 PR이 자동으로 갱신된다.

---

## 8.9 앱 실행과 서버 연결 상태 오해 가능성

### 질문

```text
앱 시작 시 Mock이 먼저 실행되는가,
아니면 서버를 먼저 호출하는가?
```

### 코드 확인 결과

```cpp
m_parkingSimulationService->seedInitialState();
m_parkingController->start();
```

따라서 Mock이 먼저 실행된다.

### 주의사항

화면에 주차 상태와 경고가 보인다는 사실만으로 Pi 서버 연결 성공을 판단할 수 없다.

서버 연결 상태는 별도의 connection status와 event를 확인해야 한다.

---

## 9. Git과 PR 정리

### 9.1 최종 커밋

```text
commit: 942463a
message: refactor: separate parking mock data and simulation
files: 15
insertions: 3090
deletions: 146
```

### 9.2 PR

```text
URL:
https://github.com/VEDA-4th-Team5/Qt_Client/pull/1

상태:
open

base:
develop

head:
taejun/rtsp-performance-reconnect
```

### 9.3 리뷰 답변 요약

```text
요청에 따라 ParkingController에서 Mock 데이터와 테스트 동작을 분리했습니다.

- Mock 값: ParkingMockData
- 시뮬레이션 동작: ParkingSimulationService
- 실제 운영 Controller: ParkingController
- 자동 테스트: parkingsimulationservice_test.cpp

전체 빌드와 CTest 3/3을 통과했습니다.
```

---

## 10. 커밋 메시지 작성 규칙 정리

오늘 Codex의 Git 커밋 지침에 사용할 기준도 정리했다.

권장 제목:

```text
<type>(<scope>): <한글 요약>
```

예시:

```text
refactor(parking): Mock 데이터와 시뮬레이션 동작 분리
```

권장 본문 항목:

```text
[변경 이유]
[주요 변경]
[실행 동작]
[설정 및 플래그]
[실행 파일 및 테스트]
[검증]
[영향]
[관련 작업]
```

언어 규칙:

```text
type/scope: 영어
제목 요약: 한글
상세 본문: 한글
파일·클래스·함수명: 실제 영문 이름 유지
```

주의:

- 기존 `942463a` 커밋에는 새 지침이 소급 적용되지 않음
- 기존 커밋을 amend하려면 force push가 필요하므로 현재 PR에서는 수행하지 않음
- 다음 커밋부터 상세 형식을 적용

---

## 11. 오늘 확정한 기술 판단

### 11.1 `ParkingController`의 정체

```text
테스트용 이름만 붙인 클래스가 아님
  ↓
실제 앱의 상태/API Controller
```

### 11.2 `processIncomingMessage()` 유지

이 함수는 샘플 데이터를 생성하지 않는다. 전달받은 정규화 문자열을 상태로 변환하는 공용 parser다.

현재는 SimulationService가 호출하지만 향후 실제 Pi Adapter에서도 호출할 수 있으므로 Controller에 유지했다.

### 11.3 Mock과 Test 구분

```text
Simulation
  → 서버 없이 앱 동작을 시연하는 기능
  → src/simulation

Automated Test
  → 코드 결과를 성공/실패로 검증
  → tests
```

### 11.4 실제 Pi 연동 방향

```text
ParkingMockData
  └─ ParkingSimulationService
           │
           ├──────────────┐
           ↓              │
Raspberry Pi Server       │
  └─ PiServerStateAdapter │
           │              │
           └──────┬───────┘
                  ↓
          ParkingController
                  ↓
                Qt UI
```

실제 schema가 확정되기 전에는 불필요한 interface/factory를 선제적으로 만들지 않는다.

---

## 12. 현재 상태

이 통합 일지를 작성하기 직전 확인 상태:

```text
branch:
taejun/rtsp-performance-reconnect

HEAD:
942463a

remote tracking:
origin/taejun/rtsp-performance-reconnect

PR:
#1 open

Qt app process:
현재 실행 중 아님

build:
PASS

CTest:
3/3 PASS
```

현재 문서는 새로 생성된 파일이므로 다음 커밋에 포함해야 한다.

---

## 13. 다음 작업 우선순위

### 13.1 즉시 가능한 작업

1. PR #1 리뷰 댓글에 변경 내용 답변
2. Re-request review
3. 이 통합 일지를 별도 docs 커밋으로 반영
4. 현재 실행 순서와 Demo 상태를 팀원에게 공유

### 13.2 Qt 다음 작업

1. Demo mode 설정 또는 build option 정의
2. Demo mode false에서 `WAITING DATA` 표시
3. `server slot_id ↔ Qt zoneId` mapping schema 정의
4. 서버 event schema와 ACK/CLEAR 규칙 반영
5. 실제 Pi Adapter 연결
6. API 정상 응답 자동 테스트 추가
7. NotificationCenter 자동 테스트 추가

### 13.3 Pi 연동 전 필요한 계약

```text
slot_id mapping
점유 상태 enum
경고 event schema
ACK/CLEAR 의미
timestamp 기준
snapshot과 실시간 event 우선순위
재연결 후 동기화 방식
```

---

## 14. 반복 실행 명령

### 빌드

```powershell
Set-Location 'C:\Users\5-15\Documents\VEDA_FINAL\Qt_Client'
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build
```

### 테스트

```powershell
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir build --output-on-failure
```

### 앱 실행

```powershell
& '.\build\smart_parking_qt_client.exe'
```

### Git 상태

```powershell
git status -sb
git diff --check
git log -5 --oneline --decorate
```

### GitHub CLI

PATH에서 `gh`를 찾지 못하면 전체 경로를 사용한다.

```powershell
& 'C:\Program Files\GitHub CLI\gh.exe' auth status
```

---

## 15. Slack 공유용 요약

```text
오늘 ParkingController에 섞여 있던 Mock/테스트 책임을 분리했습니다.

- 실제 Controller: src/controllers/parkingcontroller.*
- Mock 데이터: src/simulation/parkingmockdata.*
- 시뮬레이션 동작: src/simulation/parkingsimulationservice.*
- Qt 실행 파일: smart_parking_qt_client.exe
- 시뮬레이션 테스트: parkingsimulationservice_test.exe
- CTest: parking_simulation_service

현재 앱은 Mock 상태를 먼저 표시한 뒤 서버 API 연결을 시도합니다.
서버 성공 시 snapshot으로 교체하고 실패 시 직전 상태를 유지합니다.

전체 빌드 성공, CTest 3/3 통과했고 PR #1에 반영했습니다.
커밋: 942463a
```

---

## 16. 완료 체크리스트

- [x] 실제 Controller 역할 확인
- [x] Mock 데이터 분리
- [x] Mock/Debug 동작 분리
- [x] 상태 시각 변환 model 이동
- [x] MainWindow composition 변경
- [x] Debug signal 재연결
- [x] 새 simulation CTest 추가
- [x] 전체 build 성공
- [x] CTest 3/3 통과
- [x] 앱 실행 확인
- [x] 전체 모듈 설명서 작성
- [x] 리팩터링 상세 설계서 작성
- [x] 렌더러 전용 다이어그램 제거와 text 구조도 교체
- [x] staged diff whitespace 정리
- [x] GitHub CLI 설치 경로 확인
- [x] GitHub 인증 확인
- [x] commit과 push 완료
- [x] 기존 PR #1 갱신 확인
- [x] 커밋 메시지 작성 지침 정리
- [ ] PR 리뷰 댓글 답변
- [ ] Re-request review
- [ ] 이 통합 일지 커밋

---

## 17. 최종 결론

오늘 작업으로 `ParkingController`가 실제 운영 Controller라는 점을 코드 구조에서 명확하게 만들었다.

Mock 데이터와 시뮬레이션 동작은 각각 별도 모듈로 이동했고 자동 테스트도 `tests/`에 추가했다.

현재 앱은 Pi 서버 미완료 상황을 고려해 Mock을 먼저 주입한 후 서버 연결을 시도한다. 따라서 화면에 데이터가 표시되는 것과 서버 연결 성공은 별도로 판단해야 한다.

다음 단계의 핵심은 Mock을 다시 분리하는 것이 아니라 다음 계약을 확정하고 실제 서버 Adapter를 연결하는 것이다.

```text
server slot_id
  ↕ mapping
Qt zoneId
  +
정규화 상태/event schema
  +
PiServerStateAdapter
```

코드·테스트·문서·GitHub PR까지 오늘 변경사항은 `942463a`에 반영됐으며, 현재 문서만 후속 문서 커밋 대상으로 남아 있다.

---

## 18. 후속 작업: Parking Map 저장 알림과 종료 보호

### 18.1 작업 배경

Parking Map에는 `Unsaved changes` 문구가 있었지만 실제 변경 여부를 저장하는 상태값은 없었다. 따라서 편집 후 창을 닫아도 저장 여부를 확인할 수 없고, 저장 실패도 전체 이벤트·알림 경로에 남지 않았다.

### 18.2 구현 내용

```text
ParkingMapPage
  ├─ 실제 dirty 상태 추가
  ├─ Add/Delete/Move/Resize/Rotate/Edit/Reset 변경 감지
  ├─ Save 성공 시 dirty 해제
  ├─ Reload 시 dirty 해제
  └─ Save 실패 시 dirty 유지

MainWindow
  ├─ Save 성공/실패를 상단 배너와 Events에 기록
  ├─ Save 실패를 bell 알림으로 전달
  └─ 종료 시 Save / Discard / Cancel 제공
```

Save를 선택했는데 파일 저장이 실패하면 오류를 표시하고 종료를 취소한다. Cancel도 편집 화면을 유지하며, Discard만 저장하지 않고 종료한다.

### 18.3 테스트 보강

`parkingmappage_add_slot_test`에 다음 항목을 추가했다.

- 최초 dirty=false
- 편집 후 dirty=true
- 정상 저장 후 dirty=false
- 다시 편집 후 Reload 시 dirty=false와 저장 내용 복원
- 쓰기 실패 시 오류 반환과 dirty=true 유지

### 18.4 검증 결과

```text
[BUILD OK] smart_parking_qt_client.exe 링크 성공
[PASS] parkingcontroller_retry
[PASS] parking_simulation_service
[PASS] parkingmappage_add_slot
100% tests passed, 0 failed
```

상세 구조와 작동 방식은 다음 문서에 기록했다.

```text
docs/2026-07-22_parking_map_save_exit_protection_worklog.md
```

### 18.5 현재 미커밋 범위

이번 후속 작업의 코드, 테스트, 상세 문서와 기존 통합 일지는 아직 커밋·push하지 않았다. 사용자 확인 후 하나의 의도적인 커밋으로 묶을 수 있다.

### 18.6 슬롯 기본 모양 복원

중복된 우클릭 `Edit zone`을 제거한 자리에 `Reset to default shape`를 추가했다.

```text
복원 대상: width=84, height=58, rotation=0°
유지 대상: x/y 위치, zone ID, type, channel, IVA, hall sensor mapping
```

수정된 크기와 회전을 기본값으로 되돌리는 자동 테스트를 추가했고, 전체 build와 CTest 3/3이 통과했다. 수동 확인 후 창 없는 앱 프로세스가 남은 현상도 관찰되어, 잔류 프로세스를 종료한 뒤 정상적으로 실행 파일을 다시 링크했다.

### 18.7 회전값과 화면 표시 불일치 수정

맵에서 슬롯을 직접 회전했을 때 그래픽 항목의 회전값이 레이아웃 모델로 복사되지 않아, 상세 패널에는 `0°`가 표시되지만 맵에는 회전된 모양이 남을 수 있었다. 이 상태에서는 기본 모양 복원이 모델 값만 보고 이미 기본값이라고 판단해 실제 그래픽 항목을 갱신하지 않았다.

다음과 같이 수정했다.

- 슬롯 이동·크기·회전 완료 시 그래픽 항목의 `rotation()`을 모델에 동기화
- 기본 모양 복원 시 모델뿐 아니라 현재 그래픽 항목의 크기와 회전도 확인
- 모델 값이 이미 `0°`여도 화면 항목이 회전돼 있으면 `84 x 58`, `0°`를 강제로 적용
- 자동 테스트에 모델과 화면 회전 불일치 상태를 직접 구성하는 회귀 사례 추가

테스트 중 만든 저장되지 않은 EV-04 레이아웃 변경은 `Reload`로 폐기했으며, 실제 사용자 로컬 레이아웃 파일은 덮어쓰지 않았다. 수정 후 실행 파일 전체 링크, CTest 3개, `git diff --check`를 다시 확인했고 모두 통과했다. 새 실행 파일도 정상 기동했다.
