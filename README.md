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
