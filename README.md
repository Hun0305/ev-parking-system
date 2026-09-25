# ev-parking-system

CCTV 기반 스마트 주차장 통합 관제 시스템 설계 및 구현 | 2026 졸업작품

VEDA 4th Team5 프로젝트의 4개 하위 시스템을 하나의 저장소로 통합했습니다.
각 폴더는 원래 개별 저장소였으며, 현재는 이 저장소의 하위 디렉터리로 관리됩니다.

## 구성

- [`Pi_Server`](./Pi_Server) — Raspberry Pi 기반 C++ 서버 (MQTT, RTSP, Snapshot API, OpenCV, SQLite)
- [`Qt_Client`](./Qt_Client) — Qt/C++ 관제 클라이언트
- [`STM`](./STM) — STM32 펌웨어 (LED/Buzzer/지자기센서 연동)
- [`cv_snapshot_api`](./cv_snapshot_api) — OpenCV 기반 Snapshot/영상 처리 API

각 하위 폴더의 상세 내용은 폴더 내 README.md를 참고하세요.

## v1.0 릴리즈 노트

> 원본 저장소(`Pi_Server`, `Qt_Client`, `STM`)의 [v1.0 릴리즈](https://github.com/VEDA-4th-Team5/Pi_Server/releases/tag/v1.0)에 작성된 내용입니다.

# v1.0 — 프로토타입 배포

STM32 센서 → Raspberry Pi → 카메라/MQTT/DB로 이어지는 주차 감시·화재 감지
파이프라인의 첫 통합 프로토타입입니다.

### 🅿️ 홀센서 기반 주차 세션 관리
- STM32 UART 신호 → 세션 생성/종료 상태머신 연결 (EVDA-134)
- 점유 확인 게이트(재정렬 flap 필터) + 촬영 스케줄러 초안 (EVDA-135)
- 촬영 스케줄러 Pi 서버 통합 및 빌드 검증 (EVDA-144)
- 주차 시작/장기 점유 증거 이미지 촬영 + 세션 이미지 조회 API 연동 (EVDA-146)
- BestShot 차량 감지가 홀센서보다 먼저 세션을 만든 경우에도
  증거촬영/OCR이 정상 예약되도록 수정 (#24)

### 🔥 화재 감지 (STM32 ↔ Pi ↔ Qt)
- STM32 UART 화재 신호 수신 및 MQTT 발행 (EVDA-125)
- 센서 통신 계층 재설계: UART/LoRa 공용 전송 계층, 재연결·에러 이벤트 로깅 (EVDA-91)
- Qt-서버 간 화재 알람 MQTT 프로토콜 충돌 수정 (EVDA-159)
- 화재 알람 ACK 및 OPEN/ACKNOWLEDGED/RESOLVED 상태 전이 구현 (EVDA-170)

### 🔌 디바이스 연동
- `/dev/parking_alert` Linux Character Device 드라이버 (32면 알림 비트, ioctl/poll) (EVDA-90)

### 🗄️ 데이터/타이머 기반
- 주차 세션 DB 스키마 통합, 슬롯 ID/SQL 경로 정리 (EVDA-129)
- 장기 점유 타이머 기능 (EVDA-88)

### 🐛 v1.0 안정화
- 로그 스팸 억제(EXIT_IGNORED/HALL_SENSOR 태그별 필터링),
  중복 세션 생성 경합 조건 수정 (EVDA-167)

전체 변경: `git log main..develop` (23 커밋)

### 스크린샷

<img width="1911" height="1015" alt="image" src="https://github.com/user-attachments/assets/77506d64-e1e0-4e22-8999-fc1c4bc1632e" />

<img width="1914" height="1012" alt="image" src="https://github.com/user-attachments/assets/ab35fe93-deec-4404-bb5f-0edf2101c35e" />

<img width="1918" height="1021" alt="image" src="https://github.com/user-attachments/assets/a52a0b4b-ea95-4cff-a877-817d39bf6615" />

<img width="1912" height="1074" alt="image" src="https://github.com/user-attachments/assets/513c8680-b7a0-4d1b-88a9-cb0d040cf329" />

<img width="1916" height="1076" alt="image" src="https://github.com/user-attachments/assets/9a153d1c-7f66-4dd5-8fe7-fc8c3ad2c1c3" />

<img width="1914" height="1079" alt="image" src="https://github.com/user-attachments/assets/da65fa46-b23f-4154-b02a-a5fcb5c137fd" />

<img width="3000" height="4000" alt="image" src="https://github.com/user-attachments/assets/1ec1652e-c6cb-4e4c-97b4-77b4fcbe0a0b" />

<img width="1788" height="817" alt="image" src="https://github.com/user-attachments/assets/d662db34-3743-46ea-99e7-b72347da5f08" />

<img width="1919" height="1022" alt="image" src="https://github.com/user-attachments/assets/637eb746-0556-4fc6-a73f-277f513e8d6e" />
