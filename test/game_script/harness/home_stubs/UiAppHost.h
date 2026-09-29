#pragma once

// CoverGridHomeUi.h includes "UiAppHost.h" and "UITheme.h" by a sibling path, which in the firmware finds
// src/components/. The copy of CoverGridHomeUi the suite builds sits in another folder, where these
// two fall through to this folder and on to the screen doubles.
#include "components/UiAppHost.h"
