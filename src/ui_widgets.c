#include "src/ui_widgets.h"

#include "src/math.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

void BeginGroup(void) {
  if (IsEmptyStr8(GetNextWidgetKey())) {
    SetNextWidgetKey(Str8Literal("Group"));
  }
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (!GetNextWidgetConstraint(axis).type) {
      SetNextWidgetConstraint(
          axis, (WidgetConstraint){.type = kWidgetConstraintPercentOfParent,
                                   .value = 1.0f});
    }
  }
  SetNextWidgetLayoutAxis(kAxis2X);
  BeginWidget();
}

void EndGroup(void) { EndWidget(); }

void BeginStack(void) {
  if (IsEmptyStr8(GetNextWidgetKey())) {
    SetNextWidgetKey(Str8Literal("Stack"));
  }
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (!GetNextWidgetConstraint(axis).type) {
      SetNextWidgetConstraint(
          axis, (WidgetConstraint){.type = kWidgetConstraintPercentOfParent,
                                   .value = 1.0f});
    }
  }
  SetNextWidgetLayoutAxis(kAxis2Y);
  BeginWidget();
}

void EndStack(void) { EndWidget(); }

void SpaceBar(void) {
  if (IsEmptyStr8(GetNextWidgetKey())) {
    SetNextWidgetKey(Str8Literal("SpaceBar"));
  }
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (!GetNextWidgetConstraint(axis).type) {
      SetNextWidgetConstraint(
          axis, (WidgetConstraint){.type = kWidgetConstraintPercentOfParent,
                                   .value = 1.0f});
    }
  }
  BeginWidget();
  EndWidget();
}

void TextLine(Str8 text) {
  if (IsEmptyStr8(GetNextWidgetKey())) {
    SetNextWidgetKey(text);
  }
  for (u32 axis = 0; axis < kAxis2Count; ++axis) {
    if (!GetNextWidgetConstraint(axis).type) {
      SetNextWidgetConstraint(
          axis, (WidgetConstraint){.type = kWidgetConstraintTextContent,
                                   .strickness = 1.0f});
    }
  }
  SetNextWidgetTextContent(text);
  BeginWidget();
  EndWidget();
}
