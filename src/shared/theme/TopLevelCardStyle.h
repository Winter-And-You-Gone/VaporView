#pragma once

#include <QtGlobal>

class QWidget;

namespace VaporView
{

inline constexpr const char *kTopLevelCardProperty = "vaporViewTopLevelCard";
inline constexpr const char *kTopLevelCardShadowEffectName = "vaporViewTopLevelCardShadow";
inline constexpr int kTopLevelCardCornerRadius = 12;
inline constexpr qreal kTopLevelCardShadowBlurRadius = 22.0;
inline constexpr qreal kTopLevelCardShadowOffsetY = 6.0;
inline constexpr int kTopLevelCardShadowAlpha = 28;

void configureTopLevelCard(QWidget *card);
void updateTopLevelCardShadows(QWidget *scope, qreal uiScale = 1.0);

} // namespace VaporView
