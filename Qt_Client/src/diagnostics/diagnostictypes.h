#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QSize>
#include <QString>

struct ApiDiagnosticState
{
    bool enabled = false;
    bool connected = false;
    QString status = QStringLiteral("DISABLED");
    QString endpoint;
    QString lastError;
    QDateTime lastAttemptAt;
    QDateTime lastSuccessAt;
    int lastLatencyMs = -1;
    int lastHttpStatus = 0;
    int consecutiveFailures = 0;
    int nextRetrySeconds = 0;
    int appliedSlotCount = 0;
};

struct RtspChannelDiagnostic
{
    QString channel;
    bool configured = false;
    QString status = QStringLiteral("WAITING");
    QString error;
    QSize resolution;
    int startupDelayMs = -1;
    qint64 lastFrameWallClockMs = -1;
};

struct ParkingDiagnosticState
{
    QString dataSource = QStringLiteral("UNKNOWN");
    int slotCount = 0;
    int activeAlarmCount = 0;
    int lastServerSlotCount = 0;
    QDateTime lastServerSyncAt;
    QString lastSimulationScenario;
};

struct DiagnosticLogRecord
{
    QDateTime occurredAt;
    QString level;
    QString module;
    QString code;
    QString message;
};

Q_DECLARE_METATYPE(ApiDiagnosticState)
Q_DECLARE_METATYPE(RtspChannelDiagnostic)
Q_DECLARE_METATYPE(QList<RtspChannelDiagnostic>)
Q_DECLARE_METATYPE(ParkingDiagnosticState)
Q_DECLARE_METATYPE(DiagnosticLogRecord)
