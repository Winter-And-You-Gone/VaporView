#pragma once

#include <QtGlobal>

class QWidget;

namespace VaporView
{

inline constexpr const char *kTopLevelCardProperty = "vaporViewTopLevelCard";
inline constexpr const char *kTopLevelCardShadowLayerName = "vaporViewTopLevelCardShadowLayer";
inline constexpr int kTopLevelCardCornerRadius = 12;
inline constexpr qreal kTopLevelCardShadowBlurRadius = 16.0;
inline constexpr qreal kTopLevelCardShadowOffsetY = 4.0;
inline constexpr int kTopLevelCardShadowAlpha = 18;

void configureTopLevelCard(QWidget *card);
void updateTopLevelCardShadows(QWidget *scope, qreal uiScale = 1.0);

} // namespace VaporView
