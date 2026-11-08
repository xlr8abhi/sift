#pragma once

namespace Style {

// Marks every widget that is not visible as not yet polished, so the next stylesheet change skips it
// and it is styled when it is first shown. With many tabs open this is what keeps a theme change fast.
void deferHiddenWidgets();

// Same, but a few hundred widgets per event-loop turn, so it can run in the background after
// start-up without freezing the window. A later deferHiddenWidgets() just finishes the rest.
void deferHiddenWidgetsInBackground();

// Builds the application stylesheet from the current palette, so every theme
// (including the KDE system one) gets the same layout, spacing and shapes.
void install();

}
