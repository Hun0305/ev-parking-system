#pragma once

#include <QDialog>
#include <QString>

class FireAlarmPopup : public QDialog
{
    Q_OBJECT

public:
    explicit FireAlarmPopup(const QString &channelId, const QString &alarmId,
                            QWidget *parent = nullptr);

    const QString &channelId() const { return m_channelId; }
    const QString &alarmId() const { return m_alarmId; }
    void dismiss();

signals:
    void checkRequested(const QString &channelId, const QString &alarmId);

protected:
    void reject() override;

private:
    QString m_channelId;
    QString m_alarmId;
};
