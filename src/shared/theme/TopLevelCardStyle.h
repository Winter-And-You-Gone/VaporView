#pragma once

#include <QtGlobal>

class QWidget;

namespace VaporView
{

inline constexpr const char *kTopLevelCardProperty = "vaporViewTopLevelCard";
inline constexpr const char *kTopLevelCardShadowLayerName = "vaporViewTopLevelCardShadowLayer";
inline constexpr int kTopLevelCardCornerRadius = 12;
inline constexpr qreal kTopLevelCardShadowBlurRadius = 6.0;
inline constexpr qreal kTopLevelCardShadowOffsetY = 2.0;
inline constexpr qreal kTopLevelCardShadowVerticalSpreadExtra = 2.0;
inline constexpr int kTopLevelCardShadowAlpha = 12;

void configureTopLevelCard(QWidget *card);
void updateTopLevelCardShadows(QWidget *scope, qreal uiScale = -1.0);

} // namespace VaporView
