#include "pages/eventspage.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
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

int rowForEvent(const QTableWidget *table, const QString &eventId)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == eventId) return row;
    }
    return -1;
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
    QLabel *stateLabel = page.findChild<QLabel *>(
        QStringLiteral("eventViewStateLabel"));
    QTableWidget *table = page.findChild<QTableWidget *>(
        QStringLiteral("eventLogTable"));
    QPushButton *openEvidenceButton = page.findChild<QPushButton *>(
        QStringLiteral("eventOpenEvidenceButton"));
    QPushButton *exportButton = page.findChild<QPushButton *>(
        QStringLiteral("eventExportButton"));
    if (!searchEdit || !zoneFilter || !eventTypeFilter || !statusFilter
        || !resetButton || !resultLabel || !stateLabel || !table
        || !openEvidenceButton || !exportButton) return 1;
    if (resultLabel->text() != QStringLiteral("Showing 0 of 0 events")) return 2;
    if (resetButton->isEnabled()) return 3;
    if (stateLabel->isHidden()
        || !stateLabel->text().contains(QStringLiteral("No monitoring events"))) return 28;
    if (openEvidenceButton->isEnabled() || !table->isSortingEnabled()) return 29;
    if (table->horizontalHeader()->sectionResizeMode(3) != QHeaderView::Stretch
        || table->styleSheet().contains(QStringLiteral("#dceaf5")) == false) return 30;

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
    if (!stateLabel->isHidden()) return 31;
    if (zoneFilter->findData(QStringLiteral("EV-01")) < 0
        || zoneFilter->findData(QStringLiteral("P-01")) < 0
        || eventTypeFilter->findData(QStringLiteral("FIRE_SUSPECTED")) < 0
        || statusFilter->findData(QStringLiteral("OPEN")) < 0
        || statusFilter->findData(QStringLiteral("ACKED")) < 0
        || statusFilter->findData(QStringLiteral("CLEARED")) < 0) return 6;

    searchEdit->setText(QStringLiteral("DISCONNECTED"));
    const int sensorErrorRow = rowForEvent(
        table, QStringLiteral("P-01|HALL_SENSOR_ERROR"));
    if (sensorErrorRow < 0 || visibleRowCount(table) != 1
        || table->isRowHidden(sensorErrorRow)) return 7;
    if (!resetButton->isEnabled()) return 8;
    resetButton->click();
    if (visibleRowCount(table) != 6 || !searchEdit->text().isEmpty()) return 9;

    if (!selectFilterValue(zoneFilter, QStringLiteral("EV-01"))) return 10;
    const int overtimeRow = rowForEvent(
        table, QStringLiteral("EV-01|OVERTIME_ALERT"));
    const int ackRow = rowForEvent(table, QStringLiteral("EV-01|ALARM_ACK"));
    if (overtimeRow < 0 || ackRow < 0 || visibleRowCount(table) != 2
        || table->isRowHidden(overtimeRow) || table->isRowHidden(ackRow)) return 11;

    int navigationCount = 0;
    QString navigationEventId;
    QObject::connect(&page, &EventsPage::eventEvidenceRequested,
                     [&](const QString &eventId) {
        ++navigationCount;
        navigationEventId = eventId;
    });
    table->selectRow(overtimeRow);
    QApplication::processEvents();
    if (!openEvidenceButton->isEnabled()) return 32;
    openEvidenceButton->click();
    if (navigationCount != 1
        || navigationEventId
            != QStringLiteral("EV-01|OVERTIME_ALERT")) return 13;
    if (!QMetaObject::invokeMethod(
            table, "cellDoubleClicked", Qt::DirectConnection,
            Q_ARG(int, overtimeRow), Q_ARG(int, 2))) return 12;
    if (navigationCount != 2) return 33;

    resetButton->click();
    if (!selectFilterValue(eventTypeFilter, QStringLiteral("FIRE_SUSPECTED"))) return 14;
    const int fireOpenRow = rowForEvent(
        table, QStringLiteral("CH2|FIRE_SUSPECTED"));
    if (fireOpenRow < 0 || visibleRowCount(table) != 1
        || table->isRowHidden(fireOpenRow)) return 15;
    const QTableWidgetItem *fireStatus = table->item(fireOpenRow, 4);
    if (!fireStatus || fireStatus->background().color().name() != QStringLiteral("#ffebee")
        || !fireStatus->font().bold()) return 34;
    table->selectRow(fireOpenRow);
    QApplication::processEvents();
    if (openEvidenceButton->isEnabled()) return 35;

    resetButton->click();
    if (!selectFilterValue(statusFilter, QStringLiteral("OPEN"))) return 16;
    if (visibleRowCount(table) != 3) return 17;

    resetButton->click();
    if (!selectFilterValue(zoneFilter, QStringLiteral("CH2"))
        || !selectFilterValue(statusFilter, QStringLiteral("CLEARED"))) return 18;
    const int fireClearedRow = rowForEvent(
        table, QStringLiteral("CH2|FIRE_CLEARED"));
    if (fireClearedRow < 0 || visibleRowCount(table) != 1
        || table->isRowHidden(fireClearedRow)) return 19;
    searchEdit->setText(QStringLiteral("channel"));
    if (visibleRowCount(table) != 1 || table->isRowHidden(fireClearedRow)) return 20;

    resetButton->click();
    if (!selectFilterValue(statusFilter, QStringLiteral("CLEARED"))) return 21;
    const int sensorClearRow = rowForEvent(
        table, QStringLiteral("P-01|HALL_SENSOR_CLEAR"));
    if (sensorClearRow < 0 || visibleRowCount(table) != 2
        || table->isRowHidden(fireClearedRow)
        || table->isRowHidden(sensorClearRow)) return 22;
    page.appendEvent(makeEvent(
        QStringLiteral("EV-02"), QStringLiteral("OVERTIME_CLEAR"),
        QStringLiteral("Overtime condition cleared"), QStringLiteral("CLEARED")));
    page.appendEvent(makeEvent(
        QStringLiteral("EV-03"), QStringLiteral("NON_EV_ALERT"),
        QStringLiteral("General vehicle detected"), QStringLiteral("OPEN")));
    const int overtimeClearRow = rowForEvent(
        table, QStringLiteral("EV-02|OVERTIME_CLEAR"));
    const int nonEvOpenRow = rowForEvent(
        table, QStringLiteral("EV-03|NON_EV_ALERT"));
    if (overtimeClearRow < 0 || nonEvOpenRow < 0
        || table->rowCount() != 8 || visibleRowCount(table) != 3
        || table->isRowHidden(overtimeClearRow)
        || !table->isRowHidden(nonEvOpenRow)) return 23;
    if (resultLabel->text() != QStringLiteral("Showing 3 of 8 events")) return 24;

    resetButton->click();
    if (visibleRowCount(table) != 8
        || zoneFilter->currentIndex() != 0
        || eventTypeFilter->currentIndex() != 0
        || statusFilter->currentIndex() != 0
        || resetButton->isEnabled()) return 25;

    page.setServerState(QStringLiteral("RETRYING"));
    if (!stateLabel->isHidden()) return 36;
    page.setServerState(QStringLiteral("ERROR"), QStringLiteral("HTTP 503"));
    if (stateLabel->isHidden()
        || !stateLabel->text().contains(QStringLiteral("HTTP 503"))
        || !stateLabel->styleSheet().contains(QStringLiteral("#ffebee"))) return 37;
    page.setServerState(QStringLiteral("CONNECTED"));
    if (!stateLabel->isHidden()) return 38;

    QWidget *filterPanel = page.findChild<QWidget *>(
        QStringLiteral("eventFilterPanel"));
    page.resize(820, 620);
    page.show();
    QApplication::processEvents();
    if (!filterPanel || filterPanel->width() > page.width()
        || searchEdit->geometry().intersects(exportButton->geometry())
        || openEvidenceButton->geometry().right()
            > openEvidenceButton->parentWidget()->width()) return 39;

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
