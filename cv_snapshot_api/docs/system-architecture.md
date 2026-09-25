# CV Snapshot API 시스템 역할 경계

## 0.1.0 현재 구조

`cv_snapshot_api`는 CV5 카메라에서 현재 프레임을 JPEG로 캡처하고 OpenCV 결과를 제공하는 구성요소다. 서버와 STM은 이 프로젝트의 구현 범위가 아니다.

| 구성요소 | 책임 | 책임 밖 항목 |
|---|---|---|
| 카메라 `cv_snapshot_api` | 프레임 캡처, 환경 추정, OpenCV 처리, JPEG 응답, 채널·필터 제공 | OCR 최종 판정, LED 하드웨어 제어 |
| 서버 | 카메라 호출, JPEG 수집, OCR, 재시도와 업무 정책 결정 | LED GPIO/PWM 직접 구동 |
| STM | 명령에 따른 LED ON/OFF·밝기·안전 타이머 관리 | 이미지 처리, 환경 판단, OCR |

현재 카메라가 서버에 제공하는 운영 흐름은 다음과 같다.

```text
서버 → POST /startserver (초기 1회)
서버 → GET /channels, GET /filters (설정 또는 앱 갱신 시)
서버 → POST /images/generate
카메라 → run_id, detected_environment, auto_filter, results[].image_path
서버 → GET :{port}/images/result/jpg?run_id={run_id}&result={id}
카메라 → image/jpeg
```

## 0.2.0 개발 중 구조

저조도 OCR 재촬영은 서버가 제어하고 STM이 LED를 제어한다. 카메라는 LED 제어 API를 제공하지 않으며, 영상 기반 환경 판단 정보만 제공한다.

```text
카메라 촬영·CV → 서버 OCR
  ├─ OCR 성공: 결과 저장 후 종료
  └─ OCR 실패 및 조명 보조 조건 충족:
       서버 → STM LED ON → 안정화 대기 → 카메라 재촬영 → OCR 재시도 → STM LED OFF
```

카메라 channel과 STM LED channel의 매핑, 최대 재시도 횟수, cooldown, LED 밝기와 TTL은 서버 설정이 소유한다. STM은 TTL 만료 자동 OFF 등 하드웨어 안전 동작을 자체 보장해야 한다.

카메라의 request ID·촬영 시각·조명 권고 메타데이터는 source에 구현됐지만, CV5 build·CAP·카메라 runtime 검증 전이다. 서버 OCR과 STM LED 제어는 여전히 별도 목표 범위다.
