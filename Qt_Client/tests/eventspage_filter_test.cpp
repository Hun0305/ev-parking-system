#include "pages/eventspage.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

namespace {
MonitoringEvent makeEvent(const QString &sourceId,
                          const QString &eventType,
                          const QString &message,
                          const QString &status,
                          EventAckState ackState = EventAckState::None)
{
    MonitoringEvent event;
    event.id = sourceId + QLatin1Char('|') + eventType;
    event.occurredAt = QDateTime::fromString(
        QStringLiteral("2026-07-28T10:00:00+09:00"), Qt::ISODate);
    event.sourceId = sourceId;
    if (sourceId.startsWith(QStringLiteral("EV-"))
        || sourceId.startsWith(QStringLiteral("P-"))) {
        event.evidenceSlotId = sourceId;
    }
    event.eventType = eventType;
    event.message = message;
    event.status = status;
    event.ackState = ackState;
    return event;
}

int visibleRowCount(const QTableWidget *table)
{
    int visible = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) ++visible;
    }
    return visible;
}

bool selectFilterValue(QComboBox *comboBox, const QString &value)
{
    const int index = comboBox->findData(value, Qt::UserRole,
                                          Qt::MatchFixedString);
    if (index < 0) return false;
    comboBox->setCurrentIndex(index);
    return true;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    EventsPage page;

    QLineEdit *searchEdit = page.findChild<QLineEdit *>(
        QStringLiteral("eventSearchEdit"));
    QComboBox *zoneFilter = page.findChild<QComboBox *>(
        QStringLiteral("eventZoneFilter"));
    QComboBox *eventTypeFilter = page.findChild<QComboBox *>(
        QStringLiteral("eventTypeFilter"));
    QComboBox *statusFilter = page.findChild<QComboBox *>(
        QStringLiteral("eventStatusFilter"));
    QPushButton *resetButton = page.findChild<QPushButton *>(
        QStringLiteral("eventFilterResetButton"));
    QLabel *resultLabel = page.findChild<QLabel *>(
        QStringLiteral("eventFilterResultLabel"));
    QTableWidget *table = page.findChild<QTableWidget *>(
        QStringLiteral("eventLogTable"));
    if (!searchEdit || !zoneFilter || !eventTypeFilter || !statusFilter
        || !resetButton || !resultLabel || !table) return 1;
    if (resultLabel->text() != QStringLiteral("Showing 0 of 0 events")) return 2;
    if (resetButton->isEnabled()) return 3;

    page.appendEvent(makeEvent(
        QStringLiteral("EV-01"), QStringLiteral("OVERTIME_ALERT"),
        QStringLiteral("Parking time exceeded"), QStringLiteral("OPEN")));
    page.appendEvent(makeEvent(
        QStringLiteral("P-01"), QStringLiteral("HALL_SENSOR_ERROR"),
        QStringLiteral("Hall sensor disconnected"), QStringLiteral("OPEN")));
    page.appendEvent(makeEvent(
        QStringLiteral("EV-01"), QStringLiteral("ALARM_ACK"),
        QStringLiteral("Operator acknowledged overtime"), QString(),
        EventAckState::Acknowledged));
    page.appendEvent(makeEvent(
        QStringLiteral("CH2"), QStringLiteral("FIRE_SUSPECTED"),
        QStringLiteral("Flame detected"), QStringLiteral("OPEN")));
    page.appendEvent(makeEvent(
        QStringLiteral("CH2"), QStringLiteral("FIRE_CLEARED"),
        QStringLiteral("Channel fire cleared"), QStringLiteral("CLEARED")));
    page.appendEvent(makeEvent(
        QStringLiteral("P-01"), QStringLiteral("HALL_SENSOR_CLEAR"),
        QStringLiteral("Hall sensor recovered"), QStringLiteral("CLEARED")));

    if (table->rowCount() != 6 || visibleRowCount(table) != 6) return 4;
    if (resultLabel->text() != QStringLiteral("Showing 6 of 6 events")) return 5;
    if (zoneFilter->findData(QStringLiteral("EV-01")) < 0
        || zoneFilter->findData(QStringLiteral("P-01")) < 0
        || eventTypeFilter->findData(QStringLiteral("FIRE_SUSPECTED")) < 0
        || statusFilter->findData(QStringLiteral("OPEN")) < 0
        || statusFilter->findData(QStringLiteral("ACKED")) < 0
        || statusFilter->findData(QStringLiteral("CLEARED")) < 0) return 6;

    searchEdit->setText(QStringLiteral("DISCONNECTED"));
    if (visibleRowCount(table) != 1 || table->isRowHidden(1)) return 7;
    if (!resetButton->isEnabled()) return 8;
    resetButton->click();
    if (visibleRowCount(table) != 6 || !searchEdit->text().isEmpty()) return 9;

    if (!selectFilterValue(zoneFilter, QStringLiteral("EV-01"))) return 10;
    if (visibleRowCount(table) != 2 || table->isRowHidden(0)
        || table->isRowHidden(2)) return 11;

    int navigationCount = 0;
    QString navigationEventId;
    QObject::connect(&page, &EventsPage::eventEvidenceRequested,
                     [&](const QString &eventId) {
        ++navigationCount;
        navigationEventId = eventId;
    });
    if (!QMetaObject::invokeMethod(
            table, "cellDoubleClicked", Qt::DirectConnection,
            Q_ARG(int, 0), Q_ARG(int, 2))) return 12;
    if (navigationCount != 1
        || navigationEventId
            != QStringLiteral("EV-01|OVERTIME_ALERT")) return 13;

    resetButton->click();
    if (!selectFilterValue(eventTypeFilter, QStringLiteral("FIRE_SUSPECTED"))) return 14;
    if (visibleRowCount(table) != 1 || table->isRowHidden(3)) return 15;

    resetButton->click();
    if (!selectFilterValue(statusFilter, QStringLiteral("OPEN"))) return 16;
    if (visibleRowCount(table) != 3) return 17;

    resetButton->click();
    if (!selectFilterValue(zoneFilter, QStringLiteral("CH2"))
        || !selectFilterValue(statusFilter, QStringLiteral("CLEARED"))) return 18;
    if (visibleRowCount(table) != 1 || table->isRowHidden(4)) return 19;
    searchEdit->setText(QStringLiteral("channel"));
    if (visibleRowCount(table) != 1 || table->isRowHidden(4)) return 20;

    resetButton->click();
    if (!selectFilterValue(statusFilter, QStringLiteral("CLEARED"))) return 21;
    if (visibleRowCount(table) != 2
        || table->isRowHidden(4) || table->isRowHidden(5)) return 22;
    page.appendEvent(makeEvent(
        QStringLiteral("EV-02"), QStringLiteral("OVERTIME_CLEAR"),
        QStringLiteral("Overtime condition cleared"), QStringLiteral("CLEARED")));
    page.appendEvent(makeEvent(
        QStringLiteral("EV-03"), QStringLiteral("NON_EV_ALERT"),
        QStringLiteral("General vehicle detected"), QStringLiteral("OPEN")));
    if (table->rowCount() != 8 || visibleRowCount(table) != 3
        || table->isRowHidden(6) || !table->isRowHidden(7)) return 23;
    if (resultLabel->text() != QStringLiteral("Showing 3 of 8 events")) return 24;

    resetButton->click();
    if (visibleRowCount(table) != 8
        || zoneFilter->currentIndex() != 0
        || eventTypeFilter->currentIndex() != 0
        || statusFilter->currentIndex() != 0
        || resetButton->isEnabled()) return 25;

    QPushButton *helpButton = page.findChild<QPushButton *>(
        QStringLiteral("eventsHelpButton"));
    if (!helpButton) return 26;
    helpButton->click();
    QApplication::processEvents();
    QDialog *helpDialog = page.findChild<QDialog *>(
        QStringLiteral("eventsHelpDialog"));
    QLabel *helpSteps = helpDialog
        ? helpDialog->findChild<QLabel *>(QStringLiteral("eventsHelpSteps"))
        : nullptr;
    if (!helpDialog || !helpSteps
        || !helpSteps->text().contains(QStringLiteral("Export CSV"))) return 27;
    helpDialog->close();

    return 0;
}
