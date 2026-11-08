#pragma once

#include <QFont>

// The app-wide font size and weight. One setting drives the UI font and every monospace
// view (editors, URL bar, tables), so the whole window scales together.
namespace AppFont {

void restoreSaved();      // call once at startup, before any window exists
void setSize(qreal pt);   // 0 = the desktop's default size; saved and applied to live widgets
qreal savedSize();        // 0 when following the desktop default
qreal effectiveSize();    // the size in use right now
qreal defaultSize();      // the desktop's own size
void setWeight(int weight); // 0 = the desktop default; else a QFont weight (300 Light ... 700 Bold)
int savedWeight();
int effectiveWeight();
int defaultWeight();
QFont mono();             // the system fixed font at the current size

}
