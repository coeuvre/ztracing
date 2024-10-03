#ifndef ZTRACING_SRC_UI_WIDGETS_H_
#define ZTRACING_SRC_UI_WIDGETS_H_

#include "src/string.h"

void BeginGroup(void);
void EndGroup(void);

void BeginStack(void);
void EndStack(void);

void SpaceBar(void);

void TextLine(Str8 text);

#endif  // ZTRACING_SRC_UI_WIDGETS_H_
