#include "uifontscale.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>

namespace {
constexpr auto kBaseStyleProperty = "uiFontScaleBaseStyle";
constexpr auto kFixedScaleProperty = "uiFontScaleFixed";

bool hasFixedScaleAncestor(const QWidget *widget)
{
    for (const QWidget *current = widget; current; current = current->parentWidget()) {
        if (current->property(kFixedScaleProperty).toBool()) return true;
    }
    return false;
}

QString scaledStyleSheet(const QString &source, int percent)
{
    static const QRegularExpression expression(
        QStringLiteral("(font-size\\s*:\\s*)([0-9]+(?:\\.[0-9]+)?)(px|pt)"),
        QRegularExpression::CaseInsensitiveOption);

    QString result;
    qsizetype cursor = 0;
    auto matches = expression.globalMatch(source);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        result += source.mid(cursor, match.capturedStart() - cursor);
        const double baseSize = match.captured(2).toDouble();
        const int scaledSize = qMax(1, qRound(baseSize * percent / 100.0));
        result += match.captured(1) + QString::number(scaledSize) + match.captured(3);
        cursor = match.capturedEnd();
    }
    result += source.mid(cursor);
    return result;
}

class UiFontScaleController final : public QObject
{
public:
    UiFontScaleController(QApplication &app, const QString &configPath)
        : QObject(&app)
        , m_app(app)
        , m_configPath(configPath)
        , m_baseFont(app.font())
    {
        m_app.installEventFilter(this);
    }

    int percent() const { return m_percent; }

    void applyPercent(int percent)
    {
        m_percent = UiFontScale::normalizePercent(percent);
        m_applying = true;
        m_app.setProperty("uiFontScalePercent", m_percent);
        QFont scaledFont = m_baseFont;
        if (m_baseFont.pointSizeF() > 0.0) {
            scaledFont.setPointSizeF(m_baseFont.pointSizeF() * m_percent / 100.0);
        } else if (m_baseFont.pixelSize() > 0) {
            scaledFont.setPixelSize(
                qMax(1, qRound(m_baseFont.pixelSize() * m_percent / 100.0)));
        }
        m_app.setFont(scaledFont);
        for (QWidget *topLevel : m_app.topLevelWidgets()) {
            applyTree(topLevel, true);
        }
        m_applying = false;
    }

    bool save(QString *errorMessage)
    {
        QSettings settings(m_configPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/font_scale_percent"), m_percent);
        settings.sync();
        if (settings.status() == QSettings::NoError) return true;
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to save UI preferences to %1")
                                .arg(m_configPath);
        }
        return false;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_applying) return QObject::eventFilter(watched, event);
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget) return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show || event->type() == QEvent::Polish) {
            m_applying = true;
            applyTree(widget, false);
            m_applying = false;
        } else if (event->type() == QEvent::StyleChange) {
            m_applying = true;
            applyWidgetStyle(widget, false);
            m_applying = false;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void applyTree(QWidget *root, bool forceStoredBase)
    {
        applyWidgetStyle(root, forceStoredBase);
        const QList<QWidget *> children = root->findChildren<QWidget *>();
        for (QWidget *child : children) applyWidgetStyle(child, forceStoredBase);
    }

    void applyWidgetStyle(QWidget *widget, bool forceStoredBase)
    {
        if (hasFixedScaleAncestor(widget)) return;
        const QString current = widget->styleSheet();
        QVariant baseProperty = widget->property(kBaseStyleProperty);
        QString baseStyle;
        if (!baseProperty.isValid()) {
            baseStyle = current;
            widget->setProperty(kBaseStyleProperty, baseStyle);
        } else {
            baseStyle = baseProperty.toString();
            if (!forceStoredBase
                && current != scaledStyleSheet(baseStyle, m_percent)) {
                baseStyle = current;
                widget->setProperty(kBaseStyleProperty, baseStyle);
            }
        }

        const QString scaled = scaledStyleSheet(baseStyle, m_percent);
        if (current != scaled) widget->setStyleSheet(scaled);
    }

    QApplication &m_app;
    QString m_configPath;
    QFont m_baseFont;
    int m_percent = UiFontScale::DefaultPercent;
    bool m_applying = false;
};

UiFontScaleController *g_controller = nullptr;
}

namespace UiFontScale {

int normalizePercent(int percent)
{
    const int clamped = qBound(MinimumPercent, percent, MaximumPercent);
    return qBound(MinimumPercent,
                  qRound(clamped / static_cast<double>(StepPercent))
                      * StepPercent,
                  MaximumPercent);
}

int loadPercent(const QString &configPath)
{
    QSettings settings(configPath, QSettings::IniFormat);
    return normalizePercent(
        settings.value(QStringLiteral("ui/font_scale_percent"),
                       DefaultPercent).toInt());
}

void initialize(QApplication &app, const QString &configPath)
{
    if (!g_controller) {
        g_controller = new UiFontScaleController(app, configPath);
    }
    g_controller->applyPercent(loadPercent(configPath));
}

bool setPercent(int percent, QString *errorMessage)
{
    if (!g_controller) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("UI font scale is not initialized.");
        }
        return false;
    }
    g_controller->applyPercent(percent);
    return g_controller->save(errorMessage);
}

int currentPercent()
{
    return g_controller ? g_controller->percent() : DefaultPercent;
}

} // namespace UiFontScale
