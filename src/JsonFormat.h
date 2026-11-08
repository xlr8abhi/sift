#pragma once

#include <QString>

// Re-indents valid JSON without touching key order, strings or numbers.
// Returns the input unchanged when it is not valid JSON.
// With decodeUnicode, \uXXXX escapes in strings are shown as the real characters
// (control characters, quotes and backslashes stay escaped so the text stays valid).
QString prettyJson(const QString &text, bool decodeUnicode = false);
