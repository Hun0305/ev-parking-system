# Qt Client

Qt Widgets와 QML 기반 스마트 주차 관제 클라이언트입니다.

## 기능

- 한화비전 카메라 RTSP 4채널 표시
- 채널 확대·복귀 시 기존 스트림 유지
- EV-01~EV-03, P-01~P-04 주차면 상태 표시
- Raspberry Pi HTTP API에서 주차 상태 동기화
- 차량번호, 차량 유형, 점유 시간과 경고 표시
- 주차면 클릭 시 차량·번호판 원본/개선 이미지 조회
- API 실패 시 Mock 화면 유지
- 이벤트 로그와 CSV 내보내기
- Pi MQTT 화재 후보 알림 구독 (`parking/fire/#`)
- Settings에서 Pi 서버의 장기 점유 판정 시간 조회·변경
- CH1 공유 RTSP 프레임에서 EV01~EV04 주차 ROI 조회·편집·즉시 적용

## 실시간 MQTT 알림

Pi가 발행하는 화재 후보 알림을 구독합니다. Qt Mqtt 애드온 모듈은 팀 계정 저장소에
없어서 설치할 수 없었고, 대신 `QTcpSocket` 위에 MQTT 3.1.1을 직접 구현했습니다
(`src/services/mqttserviceclient.cpp`) — 구독 전용(발행·인증·TLS 없음)이라
추가 모듈 설치 없이 기본 Qt(Network 모듈)만으로 항상 빌드됩니다.

설정은 `config/client_config.ini`의 `[mqtt]` 섹션입니다.

~~~ini
[mqtt]
enabled=true
host=<raspberry-pi-ip>
port=1883
topics=parking/fire/#
~~~

개인 테스트용 Pi를 쓸 때는 공용 파일 대신 `config/client_config.local.ini`의
`[mqtt] host`를 덮어씁니다. 이 파일은 Git에서 제외됩니다.

화재 후보는 주차면을 `FIRE_SUSPECTED`(진한 빨강)로 표시하고 배너를 띄웁니다.
**확정이 아니라 후보이며, 클라이언트가 자동으로 확정 처리하지 않습니다.**
해제 신호가 오면 화재 직전 상태로 되돌립니다.
페이로드 규약은 Pi_Server의 `docs/MQTT_PROTOCOL_PROPOSAL.md`에 있습니다.

## 팀 공유 API 설정

config/client_config.ini은 Git으로 추적되는 팀 공용 설정입니다.

~~~ini
[api]
enabled=true
base_url=http://172.20.35.167:8080
slots_path=/api/v1/parking-slots
slot_detail_path=/api/v1/parking-slots/{slot_id}
parking_roi_list_path=/api/v1/settings/parking-slots/roi
parking_roi_path=/api/v1/settings/parking-slots/{slot_id}/roi
timeout_ms=5000
reconnect_interval_ms=5000
max_reconnect_interval_ms=60000
allow_insecure_http=true
~~~

개인 PC에서만 주소를 변경하려면 다음 파일을 복사합니다.

~~~powershell
Copy-Item ..\config\client_config.local.example.ini ..\config\client_config.local.ini
~~~

client_config.local.ini은 Git에서 제외되며, 공용 파일보다 우선합니다.

Settings 화면에서 프로토콜, `Server IP / Host`, `API Port`를 각각 입력해 저장하면
내부적으로 전체 base URL을 조합하여 로컬 오버라이드 파일에 기록하고 즉시 재연결합니다.
예를 들어 `http`, `raspberry-pi.local`, `8080`은 `http://raspberry-pi.local:8080`으로
저장됩니다. 연결 실패 시 5초부터 최대 60초까지 지수 백오프로 자동 재시도하며
`Reconnect now` 버튼으로 즉시 다시 연결할 수 있습니다.

`Parking ROI` 화면은 Dashboard에서 이미 디코딩 중인 CH1 프레임을 공유합니다.
프레임이나 미리보기 이미지를 서버에 업로드하지 않으며, 화면에서 선택한 영역을
`x`, `y`, `width`, `height`의 0~1 정규화 좌표로 변환해 Pi REST API에만 전송합니다.
화면 진입 시 SQLite에 저장된 EV01~EV04 좌표를 GET으로 다시 읽어 오버레이를 복원합니다.

## 소스 구조

- `src/mainwindow.*`: 사이드바, 페이지 전환, 화면 간 signal 연결
- `src/pages/`: Dashboard, Parking Map, Parking ROI, Events, Settings, Debug 화면
- `src/controllers/parkingcontroller.*`: API·Mock 데이터와 주차 상태/알람 처리
- `src/dialogs/slotevidencedialog.*`: 차량·번호판 이미지 증거 화면
- `src/models/parkingstate.*`: 화면에서 공유하는 주차 상태 모델
- `src/services/camerasettings.*`: 카메라 설정 파일과 RTSP URL 구성
- `src/api/`: HTTP 요청, 이미지 로딩, JSON 응답 파싱

각 페이지는 사용자 동작을 signal로 전달하고, 상태 변경은 `ParkingController`를 거쳐 화면에 반영됩니다.

## 빌드 (Windows / MinGW)

FFmpeg는 저장소에 포함되지 않습니다. [gyan.dev](https://www.gyan.dev/ffmpeg/builds/)의
`ffmpeg-*-full_build-shared` 를 받아 `third_party/ffmpeg/` 아래에 `bin` `include` `lib`
가 바로 보이도록 풀어둡니다 (최초 1회).

~~~powershell
tar -xf ffmpeg-8.0.1-full_build-shared.7z -C third_party
Rename-Item third_party\ffmpeg-8.0.1-full_build-shared ffmpeg
~~~

빌드 도구는 Qt 설치에 포함된 것을 씁니다. PATH는 새 셸을 열 때마다 지정합니다.
아래 경로의 버전(`6.11.0`, `mingw1310_64`)은 설치한 Qt에 맞춰 바꿉니다.
`C:\Qt\Tools` 와 `C:\Qt` 를 열어 실제 폴더명을 확인하세요.

~~~powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build
~~~

`build/` 캐시에는 소스 경로와 컴파일러가 고정됩니다. WSL 등 다른 툴체인으로
빌드하려면 `build/` 를 지우고 다시 configure 합니다.

## 실행

~~~powershell
$env:PATH = "C:\Qt\6.11.0\mingw_64\bin;$env:PATH"
.\build\smart_parking_qt_client.exe
~~~

FFmpeg DLL은 빌드 시 exe 옆으로 자동 복사됩니다. PATH 지정 없이 실행하려면
Qt DLL을 한 번 배포해 둡니다.

~~~powershell
C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe --qmldir qml .\build\smart_parking_qt_client.exe
~~~

API 규격은 [docs/api_v1.md](docs/api_v1.md), RTSP 동작은
[docs/rtsp_notes.md](docs/rtsp_notes.md)를 참고합니다.
