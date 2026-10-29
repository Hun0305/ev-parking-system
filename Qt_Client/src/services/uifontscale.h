#ifndef UIFONTSCALE_H
#define UIFONTSCALE_H

#include <QString>

class QApplication;

namespace UiFontScale {

inline constexpr int MinimumPercent = 90;
inline constexpr int DefaultPercent = 100;
inline constexpr int MaximumPercent = 200;
inline constexpr int StepPercent = 5;

int normalizePercent(int percent);
int loadPercent(const QString &configPath);
void initialize(QApplication &app, const QString &configPath);
bool setPercent(int percent, QString *errorMessage = nullptr);
int currentPercent();

} // namespace UiFontScale

#endif
