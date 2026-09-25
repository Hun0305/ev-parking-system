# RTSP Notes

## Hanwha Camera

기본 설정:

```ini
camera_ip=172.20.35.114
rtsp_profile=profile2
rtsp_url=rtsp://<username>:<password>@172.20.35.114:554/profile2/media.smp
```

실제 계정/비밀번호는 코드에 하드코딩하지 않습니다.

```bash
export HANWHA_CAMERA_IP=172.20.35.114
export HANWHA_CAMERA_USER=admin
export HANWHA_CAMERA_PASSWORD=your_password
```

## 4채널 논리 스트림

카메라는 1대지만 서버/UI에서는 다음 논리 채널로 분리합니다.

- CH1: 원본 영상
- CH2: 번호판 ROI
- CH3: 분석/경고 Overlay
- CH4: 이벤트 Snapshot 또는 저해상도 화면

## 현재 상태

`rtsp_legacy/RtspVideoItem.*`는 현재 Qt Client 기본 빌드에 포함되어 있으며
`qml/RtspChannel.qml`에서 채널별 RTSP 영상을 표시합니다.

앱 시작 시 카메라와 네트워크에 순간 부하가 집중되지 않도록 첫 채널은 300ms 후 시작하고,
이후 채널은 450ms 간격으로 연결합니다. 초기 연결 도중 한 채널을 확대해도 나머지 채널의
예약된 연결은 취소하지 않습니다.

## 확대·복귀 UX

- 4채널 화면에서는 각 채널의 Low profile 연결을 유지합니다.
- 한 채널을 확대해도 나머지 3개 채널의 RTSP source를 비우지 않습니다.
- 확대 상태는 레이아웃과 표시 크기만 변경하며 RTSP URL을 변경하지 않습니다.
- 4채널 화면으로 돌아올 때 재연결하지 않고 유지 중인 프레임을 다시 표시합니다.
- High profile URL은 설정에 남겨두지만 확대만으로 자동 전환하지 않습니다.

따라서 확대와 복귀 과정에서 불필요한 `Connecting` 화면과 초기 버퍼링이 발생하지 않습니다.
숨겨진 채널의 디코딩은 계속되므로 확대 중에도 평상시 4채널 관제와 동일한 네트워크·디코딩
부하를 사용합니다.

향후 고화질 전환이 필요하면 별도 HQ 동작으로 제공하고, 기존 Low 영상을 표시한 상태에서
High 스트림의 첫 프레임이 준비된 뒤 교체하는 방식으로 확장합니다.
