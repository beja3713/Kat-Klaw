#pragma once

#include "down_template.h"
#include "left_template.h"
#include "right_template.h"
#include "up_template.h"

inline constexpr int NUM_GESTURES = 4;

inline constexpr const char* gesture_names[NUM_GESTURES] = {
  "Down",
  "Left",
  "Right",
  "Up",
};

inline constexpr const float (*gesture_data[NUM_GESTURES])[6] = {
  down_template,
  left_template,
  right_template,
  up_template,
};
