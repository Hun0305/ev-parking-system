# CPU 보호 리팩터 기준

이 버전은 입력 ROI와 캡처 해상도를 변경하지 않는다. 따라서 영상 품질·FOV·픽셀 수는
릴리즈 기준과 동일하다. CPU 부하를 낮추는 방법은 요청 빈도, 동일 결과 재사용, OpenCV 내부
병렬도 제한이다.

## 기본 설정

`app/src/sample_component/manifests/SampleComponent_default_attribute_0.json`:

| Attribute | 기본값 | 의미 |
|---|---:|---|
| `opencv_thread_limit` | 1 | OpenCV 내부 worker 수 상한 |
| `processing_min_interval_ms` | 2500 | 전체 채널에서 무거운 처리 시작 사이의 최소 간격 |
| `processed_jpeg_cache_ttl_ms` | 2500 | `GET /image/jpg`가 새 캡처 없이 최근 JPEG를 반환하는 기간 |

값은 persisted Attribute가 있으면 그 값이 우선한다. `0`은 간격 또는 cache를 끄므로
CPU 보호 설정으로 사용하면 안 된다. 최대 허용값은 60,000 ms이며, 스레드 수는 최소 1이다.

## API 동작

- `POST /process`, `POST /images/generate`, `POST /environment/analyze`와
  `POST /environment/test`는 같은 단일 admission slot을 사용한다.
- slot이 실행 중이면 HTTP 503 `PROCESSING_BUSY`를 반환한다.
- slot은 비어 있지만 마지막 시작 후 `processing_min_interval_ms`가 지나지 않았으면 HTTP 429
  `PROCESSING_RATE_LIMITED`를 반환한다. 응답 `message`의 `retry after N ms` 이후에만 재시도한다.
- `GET /image/jpg`는 fresh cache를 즉시 반환한다. cache가 오래됐어도 slot이 busy/rate-limited면
  이전 JPEG를 반환한다. 이전 JPEG가 하나도 없는 최초 요청만 429 또는 503을 받을 수 있다.

호출자는 429/503을 즉시 재시도하지 말고 응답의 대기 시간 이상으로 exponential backoff 해야 한다.
`/images/generate`의 기본 출력은 이미 `original`과 `auto` 두 장이며, 추가 필터는 명시한
`outputs`일 때만 요청한다.

## 검증 절차

1. 새 CAP 설치 전, 기존 앱의 종료 직후 kernel/app 로그에서 `oom`, `watchdog`, `thermal`을
   구분한다. CPU 수치만으로 종료 원인을 단정하지 않는다.
2. 새 CAP에서 초기 `GET /image/jpg` 한 번 후 2.5초 안에 같은 요청을 반복한다. 첫 요청만
   `SNAPSHOT`/`OPENCV` 로그가 있고 이후에는 `cache hit`이 나와야 한다.
3. 4채널 요청은 최소 2.5초 간격으로 보낸다. 429가 나오면 클라이언트가 기다린 뒤 재시도해야 한다.
4. 목표 요청률에서 10분 이상 CPU, temperature, RSS, thread 수와 앱 재시작 여부를 함께 기록한다.
   watchdog/thermal/OOM이 사라졌는지는 이 장비 검증 후에만 판정한다.

OS cgroup CPU quota나 nice는 위 동작으로 처리 시간이 안정된 뒤의 보조 안전장치다. watchdog이
있는 카메라에서 quota만 먼저 낮추면 응답 지연으로 재시작될 수 있으므로, 실제 제한값은 장비
로그와 처리시간 측정 후 정한다.
