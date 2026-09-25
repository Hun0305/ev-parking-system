# RTSP Legacy Reference

원본 `Gyuseok_version`에서 선별 복사한 RTSP/FFmpeg/QML 참고 코드입니다.

- `RtspVideoItem.h`, `RtspVideoItem.cpp`: FFmpeg로 RTSP 영상을 디코딩해 `QQuickPaintedItem`에 그리는 코드
- `qml_source/`: 고화질/저화질 RTSP 모니터링 QML UI

기본 통합 Qt 클라이언트는 Widgets UI이므로 이 코드는 빌드 대상에서 제외했습니다. FFmpeg 바이너리 전체 번들은 용량과 플랫폼 의존성이 커서 복사하지 않았습니다.
