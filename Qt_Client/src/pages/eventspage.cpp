#include "eventspage.h"

#include "widgets/pagehelp.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

EventsPage::EventsPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *filterLayout = new QHBoxLayout;
    filterLayout->setSpacing(8);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("eventSearchEdit"));
    m_searchEdit->setPlaceholderText(
        QStringLiteral("Search time, zone, event, message, or status"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(260);
    filterLayout->addWidget(m_searchEdit, 1);

    m_zoneFilter = new QComboBox(this);
    m_zoneFilter->setObjectName(QStringLiteral("eventZoneFilter"));
    m_zoneFilter->addItem(QStringLiteral("All zones"), QString());
    m_zoneFilter->setMinimumWidth(120);
    filterLayout->addWidget(m_zoneFilter);

    m_eventTypeFilter = new QComboBox(this);
    m_eventTypeFilter->setObjectName(QStringLiteral("eventTypeFilter"));
    m_eventTypeFilter->addItem(QStringLiteral("All events"), QString());
    m_eventTypeFilter->setMinimumWidth(170);
    filterLayout->addWidget(m_eventTypeFilter);

    m_statusFilter = new QComboBox(this);
    m_statusFilter->setObjectName(QStringLiteral("eventStatusFilter"));
    m_statusFilter->addItem(QStringLiteral("All statuses"), QString());
    addFilterOption(m_statusFilter, QStringLiteral("OPEN"));
    addFilterOption(m_statusFilter, QStringLiteral("ACKED"));
    addFilterOption(m_statusFilter, QStringLiteral("CLEARED"));
    m_statusFilter->setMinimumWidth(130);
    filterLayout->addWidget(m_statusFilter);

    m_resetFilterButton = new QPushButton(QStringLiteral("Reset filters"), this);
    m_resetFilterButton->setObjectName(QStringLiteral("eventFilterResetButton"));
    m_resetFilterButton->setEnabled(false);
    filterLayout->addWidget(m_resetFilterButton);
    filterLayout->addWidget(createPageHelpButton(
        this, this,
        {QStringLiteral("events"), QStringLiteral("Events"),
         QStringLiteral("Events 사용 안내"),
         QStringLiteral("수신한 정규화 이벤트를 검색·필터링하고 전체 이력을 내보냅니다."),
         QStringLiteral(
             "<b>1. 이벤트 검색</b><br>검색창은 시간, Zone, Event, Message, Status 전체 열을 대상으로 합니다.<br><br>"
             "<b>2. 조건 필터링</b><br>Zone, 이벤트 종류, OPEN·ACKED·CLEARED 상태 필터는 검색어와 함께 적용됩니다.<br><br>"
             "<b>3. 증거 화면 이동</b><br>주차 슬롯 이벤트를 더블클릭하면 해당 슬롯의 <i>Evidence</i> 화면으로 이동합니다.<br><br>"
             "<b>4. CSV 저장</b><br><i>Export CSV</i>를 누르면 현재 필터와 관계없이 전체 이벤트 로그를 저장합니다."),
         QStringLiteral(
             "※ 화재 채널이나 SYSTEM처럼 주차 슬롯으로 연결되지 않는 이벤트는 Evidence로 이동하지 않습니다.\n"
             "   Reset filters는 검색어와 모든 필터를 초기화합니다.")}));
    layout->addLayout(filterLayout);

    m_eventTable = new QTableWidget(0, 5, this);
    m_eventTable->setObjectName(QStringLiteral("eventLogTable"));
    m_eventTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Zone"), QStringLiteral("Event"), QStringLiteral("Message"), QStringLiteral("Status")});
    m_eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventTable->verticalHeader()->setVisible(false);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_eventTable, 1);
    auto *buttonLayout = new QHBoxLayout;
    auto *hint = new QLabel(
        QStringLiteral("Double-click an event to open evidence by event ID."), this);
    hint->setStyleSheet(QStringLiteral("color:#607d8b;"));
    buttonLayout->addWidget(hint);
    m_filterResultLabel = new QLabel(QStringLiteral("Showing 0 of 0 events"), this);
    m_filterResultLabel->setObjectName(QStringLiteral("eventFilterResultLabel"));
    m_filterResultLabel->setStyleSheet(QStringLiteral("color:#607d8b;"));
    buttonLayout->addWidget(m_filterResultLabel);
    buttonLayout->addStretch();
    auto *exportButton = new QPushButton(QStringLiteral("Export CSV"), this);
    exportButton->setToolTip(
        QStringLiteral("Export the complete event log, regardless of filters"));
    buttonLayout->addWidget(exportButton);
    layout->addLayout(buttonLayout);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &EventsPage::applyFilters);
    connect(m_zoneFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_eventTypeFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_statusFilter, &QComboBox::currentIndexChanged,
            this, &EventsPage::applyFilters);
    connect(m_resetFilterButton, &QPushButton::clicked,
            this, &EventsPage::resetFilters);
    connect(exportButton, &QPushButton::clicked, this, &EventsPage::exportCsv);
    connect(m_eventTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                const QTableWidgetItem *eventItem = m_eventTable->item(row, 0);
                const QString eventId = eventItem
                    ? eventItem->data(Qt::UserRole).toString().trimmed()
                    : QString();
                if (!eventId.isEmpty()) {
                    emit eventEvidenceRequested(eventId);
                }
            });
}

void EventsPage::appendEvent(const MonitoringEvent &event)
{
    const int row = m_eventTable->rowCount();
    m_eventTable->insertRow(row);
    const QStringList values = {
        monitoringEventTimeText(event), event.sourceId, event.eventType,
        event.message, monitoringEventStatusText(event)};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setData(Qt::UserRole, event.id);
        item->setData(Qt::UserRole + 1, event.evidenceSlotId);
        m_eventTable->setItem(row, column, item);
    }
    addFilterOption(m_zoneFilter, event.sourceId);
    addFilterOption(m_eventTypeFilter, event.eventType);
    addFilterOption(m_statusFilter, values.at(4));
    applyFilters();
    if (!m_eventTable->isRowHidden(row)) {
        m_eventTable->scrollToBottom();
    }
}

void EventsPage::applyFilters()
{
    const QString searchText = m_searchEdit->text().trimmed();
    const QString zone = m_zoneFilter->currentData().toString();
    const QString eventType = m_eventTypeFilter->currentData().toString();
    const QString status = m_statusFilter->currentData().toString();
    int visibleCount = 0;

    for (int row = 0; row < m_eventTable->rowCount(); ++row) {
        const auto columnText = [this, row](int column) {
            const QTableWidgetItem *item = m_eventTable->item(row, column);
            return item ? item->text().trimmed() : QString();
        };

        bool matches = zone.isEmpty()
            || columnText(1).compare(zone, Qt::CaseInsensitive) == 0;
        matches = matches && (eventType.isEmpty()
            || columnText(2).compare(eventType, Qt::CaseInsensitive) == 0);
        matches = matches && (status.isEmpty()
            || columnText(4).compare(status, Qt::CaseInsensitive) == 0);

        if (matches && !searchText.isEmpty()) {
            matches = false;
            for (int column = 0; column < m_eventTable->columnCount(); ++column) {
                if (columnText(column).contains(searchText, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
            }
        }

        m_eventTable->setRowHidden(row, !matches);
        if (matches) ++visibleCount;
    }

    m_filterResultLabel->setText(
        QStringLiteral("Showing %1 of %2 events")
            .arg(visibleCount)
            .arg(m_eventTable->rowCount()));
    m_resetFilterButton->setEnabled(
        !searchText.isEmpty() || !zone.isEmpty()
        || !eventType.isEmpty() || !status.isEmpty());
}

void EventsPage::resetFilters()
{
    const QSignalBlocker searchBlocker(m_searchEdit);
    const QSignalBlocker zoneBlocker(m_zoneFilter);
    const QSignalBlocker eventBlocker(m_eventTypeFilter);
    const QSignalBlocker statusBlocker(m_statusFilter);
    m_searchEdit->clear();
    m_zoneFilter->setCurrentIndex(0);
    m_eventTypeFilter->setCurrentIndex(0);
    m_statusFilter->setCurrentIndex(0);
    applyFilters();
}

void EventsPage::addFilterOption(QComboBox *comboBox, const QString &value)
{
    const QString displayValue = value.trimmed();
    if (displayValue.isEmpty()) return;

    for (int index = 1; index < comboBox->count(); ++index) {
        if (comboBox->itemData(index).toString().compare(
                displayValue, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    comboBox->addItem(displayValue, displayValue);
}

void EventsPage::exportCsv()
{
    const QString defaultName = QStringLiteral("parking_event_log_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".csv");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Event Log CSV"), defaultName, QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportResult(false, QStringLiteral("Could not open CSV file: ") + path);
        return;
    }
    auto escape = [](QString value) {
        value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    };
    file.write("\xEF\xBB\xBF");
    QStringList headers;
    for (int column = 0; column < m_eventTable->columnCount(); ++column) {
        headers << escape(m_eventTable->horizontalHeaderItem(column)->text());
    }
    file.write(headers.join(QLatin1Char(',')).toUtf8());
    file.write("\n");
    for (int row = 0; row < m_eventTable->rowCount(); ++row) {
        QStringList values;
        for (int column = 0; column < m_eventTable->columnCount(); ++column) {
            const QTableWidgetItem *item = m_eventTable->item(row, column);
            values << escape(item ? item->text() : QString());
        }
        file.write(values.join(QLatin1Char(',')).toUtf8());
        file.write("\n");
    }
    emit exportResult(true, QStringLiteral("Event log CSV exported: ") + path);
}
