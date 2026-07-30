#pragma once

#include <QSettings>
#include <QString>

namespace VaporView
{

QString applicationConfigFilePath();
QSettings applicationConfigSettings();
void migrateLegacyApplicationConfig();

} // namespace VaporView
