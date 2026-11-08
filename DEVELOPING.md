# Developing sift

Technical guide: how the project is laid out, how to build and debug it, how to
change it safely, and the traps to avoid. Read `ABOUT.md` first for the purpose and
the rules a change must respect.

## Stack and build

- C++20, Qt 6 (Widgets, Network), CMake 3.21+, AUTOMOC.
- Debian / Ubuntu build dependencies:

```bash
sudo apt install build-essential cmake qt6-base-dev
```

Build and run:

```bash
cmake -S . -B build
cmake --build build
./build/sift
```

After adding or removing a source file, edit `CMakeLists.txt` (sources are listed
explicitly) and re-run `cmake -S . -B build`.

There is no test suite in the repository. See "Testing" below for the technique used
during development.

## Source map

All code is in `src/`. About 8,200 lines in total.

| File(s) | Responsibility |
| --- | --- |
| `main.cpp` | Creates the app, restores font and theme, installs the stylesheet, shows the window |
| `MainWindow` | Window, menus and shortcuts, document tabs, History panel, Settings panel (inside a scroll area), session save/restore, the F1 help list |
| `RequestTab` | One request/response tab: URL bar, request editors, sending, response views and states, search, render view, status line. The largest file, and home of several small helper classes (listed below) |
| `KeyValueTable` | The Params / Headers / Form table (checkbox, key, value). Single-click editing, no column header (the dashed "Add key / Add value" row at the bottom names the columns). At rest a Key / Value cell is one line cut with "..."; the cell being edited wraps and grows up to 6 lines, then scrolls (the row shrinks back when editing ends); the editor is a `CellEdit` (`QPlainTextEdit`, styled by `#cellEdit` in `Style.cpp`): Enter commits, Tab/Shift+Tab move on, Esc cancels, pasted line breaks become spaces. Hovering a row washes it and shows a trash icon at the end of the Value cell (`RowDelegate::drawTrash`, hit area `KeyValueTable::trashRect`); clicking it deletes the row (`removeRowAt`), as the `Delete` key does for selected rows. The empty "Add" row has none. Row heights come from `RowDelegate::sizeHint` and are refreshed on column resize and edit |
| `RequestPreview` | The content of the Preview tab (a plain widget, not a popup): raw HTTP and cURL text, `C` to copy |
| `BuiltRequest.h` | Plain struct for a fully built request (method, URL, headers, body) |
| `CurlParser` | Parses a pasted cURL command into a request |
| `JsonFormat` | Order-preserving JSON pretty-printer |
| `JsonHighlighter` | JSON syntax colours |
| `HtmlHighlighter` | HTML / XML syntax colours |
| `FoldingEdit` | Read-only code view with line numbers, fold arrows, indent guides, item/key counts, the hover tooltip, and the JSON path of the current line |
| `ResponseWidgets` | `IconButton` (painted search / wrap / copy buttons of the status bar), `PathBar` (the path line under Pretty) and `KineticScroll` (touchpad momentum, pixel glide) |
| `HistoryStore` | Saves, loads and removes history entries |
| `HistoryList` | `HistoryTree` (the History list, hover and remove button, minute tick) and `HistoryDelegate` (paints each row) |
| `Theme` | Theme loading (KDE system or NvChad base46), palette building, semantic colours, method colours |
| `ThemePicker` | The `Alt+T` theme dialog with live preview (the preview is debounced, see "Themes") |
| `Style` | Builds the application-wide Qt stylesheet from the current palette, and defers styling of hidden widgets (`deferHiddenWidgets`, `ShowPolisher`) so theme changes stay fast with many tabs |
| `AppFont` | App-wide font size and weight, and the shared monospace font |
| `SettingsWidgets` | Custom-painted Settings controls: `Switch`, `Segmented`, `ThemeRow`, `FontPreview` |
| `DocTabBar` | The document tab bar (`DocTabBar`, `DocTabWidget`) and the rows of the overflow list (`TabListRow`) |
| `SideRail` | The left icon rail (History at the top, Settings at the bottom) and its custom-painted buttons |

### Helper classes inside `RequestTab.cpp`

These live in the anonymous namespace at the top of the file. They are small and tied to
this one widget, which is why they are not separate files yet.

| Class | Purpose |
| --- | --- |
| `CountTabBar`, `RequestTabWidget` | Tab bar that draws "Params 2" / "Headers 12" as one caption, so the tab is exactly as wide as its text. `setDot` shows a dot instead of a number (used by Body) |
| `Spinner` | Rotating arc on a round disc; its timer runs only while visible |
| `EmptyState` | Response pane before the first send ("Not sent") |
| `PendingState` | Response pane while a request is running (large spinner and a Cancel button) |
| `CancelledState` | Response pane after a cancel |
| `DropCombo`, `MethodDelegate` | Combos whose list opens straight below the box with a rounded hover / current wash. The method box uses it with each verb in its colour (`verbs=true`); the Body `Type` box uses it with plain text (`verbs=false`) and `AdjustToContents`, so box and list are as wide as the widest item |
| `RenderView` | `QTextBrowser` that fetches images in the background (the Render tab) |
| `HeaderHighlighter` | Colours the Raw view inside the Headers tab (name, colon, value) |
| `HeadersPane` | Body of the response Headers tab: Table view (two columns, grouped, cookies split) and Raw view, filter box, copy button |

### App icon and launcher (`data/`)

| File | Purpose |
| --- | --- |
| `data/sift.svg` | The app icon: an "s" between braces on a dark rounded tile |
| `data/sift.desktop` | The app-menu entry. Its `Exec` is an absolute path to `build/sift`, so edit it if the project moves |

`main.cpp` calls `setDesktopFileName("sift")` so the shell matches the window to the launcher,
and sets the window icon from the installed theme icon `sift`, falling back to
`data/sift.svg` next to the build folder. `CMakeLists.txt` has `install()` rules for the binary,
the desktop file and the icon.

For a per-user install without `cmake --install`, copy the two files and refresh the caches:

```bash
cp data/sift.svg ~/.local/share/icons/hicolor/scalable/apps/sift.svg
cp data/sift.desktop ~/.local/share/applications/sift.desktop
update-desktop-database ~/.local/share/applications
gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor
kbuildsycoca6
```

### How the pieces connect

```
main.cpp
  AppFont::restoreSaved -> ThemeManager::restoreSaved -> Style::install
  MainWindow
    SideRail  --selects-->  QStackedWidget { History panel | Settings panel }
    QTabWidget "docTabs"  --one per-->  RequestTab
      RequestTab
        URL bar:  method combo + QLineEdit + Send
        request tabs "reqTabs":  Preview | Params | Headers | Body   (a new request starts on Params)
        response stack (m_responseStack), one page per state:
            0 EmptyState      never sent
            1 status row + response tabs "respTabs"   (a response, or an error)
            2 PendingState    request running
            3 CancelledState  request cancelled
        response tabs:  Pretty | Raw | Headers | Render (HTML only)
        QNetworkAccessManager (one per tab, so one cookie jar per tab)
```

`MainWindow` listens to each tab's signals (`completed`, `summaryChanged`) to write
history and update the tab title. `ThemeManager::changed` is a signal many widgets
listen to so they can recolour.

## Where data lives

All plain files, all local.

| Data | Location |
| --- | --- |
| Settings (theme, font size/weight, side by side, wrap, menu bar) | `~/.config/sift/sift.ini` |
| History | `~/.local/share/sift/history/`, one JSON file per request |
| Open tabs | `~/.local/share/sift/session.json` (written on close, restored on start) |
| NvChad themes | `~/.local/share/nvim/lazy/base46/lua/base46/themes/` and `~/.config/sift/themes/` (own themes override) |
| Stylesheet icons | `~/.cache/sift/` (small generated PNGs, safe to delete) |

Session and history are written with `QSaveFile`, so a crash mid-write cannot corrupt
them. Never change a file format without keeping old files readable.

## Request lifecycle

1. The user presses `Ctrl+Enter` (or Send) and `RequestTab::send()` runs.
2. `buildRequest()` turns the editors into a `BuiltRequest`: it merges the URL query
   with enabled params, adds enabled headers, and sets the body and an automatic
   `Content-Type` unless the user set one.
3. The request goes out through the tab's `QNetworkAccessManager`. While it runs the
   Send button becomes Cancel, the response pane shows `PendingState`, and `Esc` (or the
   pane's Cancel button) calls `cancel()`.
4. `onFinished()` fills the Raw, Headers, Pretty and Render views, picks highlighting
   (JSON, markup, or none), opens Pretty for JSON and HTML / XML and Raw for any other text, sets the status chip and the status line, and emits `completed`.
5. `MainWindow` stores the entry in history through `HistoryStore`.

Cancelled requests are not recorded in history. A cancel shows `CancelledState`.

### Which response page is shown

`RequestTab::setBadge(text, kind)` is the single place that decides. The badge kind picks
the page: Idle shows `EmptyState`, Pending shows `PendingState`, anything else shows the
tabs, except that `m_cancelled` (set by `cancel()`, cleared by `send()`, `onFinished()` and
`setIdleStatus()`) swaps in `CancelledState`. `onThemeChanged()` calls `setBadge` again to
recolour the chip, so any state that must survive a theme change has to be a member, not
something inferred from the text.

To add a new state, add a page to `m_responseStack`, add a flag or badge kind, and extend
that one `setCurrentIndex` line.

### The status line

The row above the response tabs is the status chip (for example `200 OK`) followed by one
label, `m_meta`, written by `updateMeta()` as `size time age` (separated by two
spaces, no commas), for example `90.49 KB  334ms  2m ago`.

- Size uses `KB` and `MB` (`formatSize`).
- Age counts from the moment the response arrived: under a minute `now`, then `Nm ago`,
  then `Nh Mm ago` (just `Nh ago` on the hour) up to 3 hours, then `long ago`.
- The status chip shows the reason phrase; HTTP/2 sends none, so `statusPhrase()` in `RequestTab.cpp`
  fills in the standard text for common codes (that text is also what history stores).
- The right end of the row holds three `IconButton`s: search (`openSearch`), wrap (checkable, emits
  `wrapToggled`; `MainWindow` routes it through the Settings switch so the View action, the switch and
  every tab stay in sync) and copy response (`copyResponse`).
- A 20 second timer (`m_ageTimer`) refreshes the age while a response is shown. It is stopped
  by `clearMeta()`, so it does not run for empty or cancelled tabs.
- A cancelled request shows only the elapsed time.

## Styling and theming

Colours flow in one direction:

```
Theme (KDE palette or NvChad file)
  -> ThemeManager::buildPalette  -> QApplication::setPalette
  -> Style::install              -> one application stylesheet built from that palette
  -> widgets keep object names ("urlBar", "docTabs", "reqTabs", "respTabs", "sendButton",
     "panelTitle", "meta", "fieldLabel", "sectionLabel", "render") that the stylesheet targets
```

To restyle something:

- Prefer editing the stylesheet in `Style.cpp`. Use the colour tokens (`%BASE%`, `%WIN%`,
  `%TX%`, `%DM%`, `%LN%`, `%P2%`, `%AC%`, `%WASH%` ...) so every theme works. Never
  hard-code a colour in the stylesheet.
- To target a new widget, give it an `objectName` and add a rule for that id.
- Semantic colours (OK green, error red, method colours, JSON colours) come from
  `ThemeManager::colors()` and `methodColor()`. Use those in code instead of literals.
- Anything painted by hand (`SideRail`, `FoldingEdit`, the empty state, the table's
  "add" row) must read colours from the widget palette, not constants.

### Traps with the stylesheet

- **A stylesheet overrides the palette and `setTabTextColor`.** The document tab labels are
  painted by `DocTabBar` (see "Document tabs"), so the `docTabs` rules deliberately do not set `color`.
  If you add a `color` rule to a widget that code colours, the code's colour is lost.
- **Replacing a live stylesheet is slow** (about 240 ms). `Style::install` clears it first
  and then sets the new one, which is about four times faster. Keep it that way, and do
  not call `Style::install` more often than needed.
- **Cost grows with the number of open tabs.** A stylesheet change restyles every widget that is
  already polished, and one request tab is about 175 widgets (60 tabs: about 10,600 widgets, 5 to 9 s
  per theme change when measured). `Style::deferHiddenWidgets()` therefore unpolishes every widget
  that is not visible (`style()->unpolish()` plus clearing `WA_WState_Polished`) before the
  stylesheet is cleared and set. Hidden tabs are skipped and are polished with the current
  stylesheet and palette the moment they are shown (about 100 ms the first time). With 60 tabs a
  theme change now takes well under 1.5 s. It runs at the top of `ThemeManager::apply` only;
  keep the stylesheet cleared first (`apply` does) and do not add per-tab work to the
  `ThemeManager::changed` handlers.
- **Do not make the unpolish conditional on `WA_WState_Polished`.** Setting a stylesheet polishes
  every widget through `QStyleSheetStyle` (including hidden ones) without setting that flag, so a
  hidden widget can look unpolished yet hold the previous theme's palette. Skipping it left the Params
  table of a hidden tab dark after a dark theme then a light one. `deferHiddenWidgets()` unpolishes
  all hidden widgets every time, and must run before the stylesheet is cleared (not after, when
  `style()` is the plain base style and cannot undo the stylesheet's palette).
- **Qt 6 does not re-polish on show after we unpolish.** `QWidget::ensurePolished()` is keyed on an
  internal pointer, not on `WA_WState_Polished`, so clearing the attribute is not enough: an unpolished
  widget that is later shown stayed unstyled (a response viewer stayed dark after Ayu Light then
  System). `Style.cpp` therefore keeps a set of deferred widgets and an application event filter
  (`ShowPolisher`) that calls `style()->polish(w)` itself on the widget's first `Show` event. Any new
  way of unpolishing widgets must go through `unpolishLater()`.
- **Pay the unpolish early.** On a window with many tabs that first unpolish takes seconds (4.5 s for
  60 tabs). `MainWindow` runs `Style::deferHiddenWidgetsInBackground()` 400 ms after start-up, which
  does it 150 widgets per event-loop turn, so a theme change after that takes about 0.4 s.
- **Image URLs in a stylesheet are cached by file name.** Generated icons include the
  colour in their file name so a theme change loads the right one.
- **Combo box popups.** The method list uses a custom delegate (colour and hover) and a
  custom `showPopup` position. A stylesheet on a combo popup replaces the item drawing
  and drops hover feedback, so anything custom needs its own hover handling.
- **Counts in tabs are painted by `CountTabBar`, not by tab buttons.** A widget placed in a
  tab with `setTabButton` leaves unwanted space and is sized before its text is set, which
  made the underline too wide. Painting the caption yourself and widening `tabSizeHint`
  keeps the tab exactly as wide as its text. If you add a count to another tab, extend
  `CountTabBar` (it handles tabs 0 and 1).
- **A stylesheet on `QTextBrowser` would override its white canvas.** The Render view has
  `objectName "render"` and its own rule with a white background and black text, and it is
  deliberately left out of the generic rules.
- **Table cells.** `KeyValueTable` paints its last row with a custom delegate, so the cell
  stylesheet and the delegate must agree on the row height (34 px).

## Fonts

`AppFont` owns one size and one weight (0 means "use the desktop default").

- Use `AppFont::mono()` for any monospace widget. Never call
  `QFontDatabase::systemFont(FixedFont)` directly, or the widget will ignore the
  Settings panel.
- Changing the setting updates the application font and every existing widget whose
  family is the mono family. A widget that must keep its own weight (the method box is
  always bold) sets the dynamic property `sift_bold`.
- Per-view Ctrl+scroll zoom is separate and is reset when the size setting changes.
- The Settings panel offers 8 to 20 pt for size (stepper) and Default, Light, Normal, Bold for weight.
  "Reset font to defaults" clears both.

## Zoom limits (important)

Qt's own zoom has no lower bound. Trackpad scrolling shrinks the font to almost
nothing, every line then fits on screen, and the app freezes laying out the entire
response. Every zoomable view therefore goes through an event filter that clamps the
font to **6 to 36 pt**:

- `RequestTab::eventFilter` handles Pretty, Raw, Headers and Render.
- `RequestPreview::eventFilter` handles the HTTP and cURL views.

Any new text view that can be zoomed **must** install the same clamp.

## Response views

- **Highlighters.** A `QPlainTextEdit` document can hold only one highlighter at a
  time, because the layout keeps one set of formats per block. `RequestTab` therefore
  keeps the JSON and HTML highlighters detached (`setDocument(nullptr)`) and attaches
  exactly one with `setHighlighting(json, markup)`. Never attach two to one document.
- **Size caps.** Highlighting and the Render tab are skipped above 2 MB
  (`kMaxHighlightBytes`).
- **JSON.** `QJsonDocument::toJson` sorts keys alphabetically, so `prettyJson()` is used to
  keep the server's key order and number text. Invalid JSON is returned unchanged.
- **Folding.** `FoldingEdit` finds folds from lines ending in `{` or `[` matched with a
  closing line, and hides blocks with `QTextBlock::setVisible`. Call `resetFolds()`
  after every `setPlainText`.
- **Render tab.** It uses `QTextBrowser`, which supports only a CSS2-like subset. There is
  no JavaScript. Images and linked stylesheets are fetched in the background with the
  tab's own network manager (limits: 150 images, 8 MB each, 12 stylesheets, 2 MB each;
  http and https only). Stylesheets are simplified first: `@media`, `@import`,
  `@font-face` and `@keyframes` are stripped because Qt's parser mishandles them.
  Pending fetches are cancelled whenever a new response arrives (generation counter).

### Document tabs

`DocTabBar` replaces the plain `QTabBar` of `docTabs` (through `DocTabWidget`). Qt still draws the
tab shape, hover and selected colours from the stylesheet (`CE_TabBarTabShape`); the class paints
the label itself:

- A small bold method badge (about 78% of the font size, in the method colour) and the title: host
  in the normal colour, path muted. All badges reserve the width of "DELETE", so titles line up.
- An untouched "New request" tab is drawn muted (data key `fresh`).
- The `x` is painted by the bar, not a close-button widget, and only shows on the current and the
  hovered tab. A click on it emits `tabCloseRequested`; `MainWindow::closeTab` is unchanged.
  `setTabsClosable` must stay off, or Qt adds its own buttons on top.
- What to draw comes from `setTabData` (a map built by `DocTab::data`), set in
  `MainWindow::updateTabTitle`. The plain tab text is still set for accessibility. Because the colour
  is painted, `setTabTextColor` is no longer used and a theme change only needs `updateTabTitle`/repaint.
- Tab width is computed in `tabSizeHint` from the title and clamped to 110 to 220 px (`kMinTab`,
  `kMaxTab`); longer titles are cut with an ellipsis. The stylesheet's `min-width` / `max-width` do
  not apply to a size we return ourselves, so the clamp has to live in code.

**Overflow list.** The corner widget holds `+` and a `tabList` button (a count and a menu arrow).
`MainWindow::updateTabOverflow()` shows the button only when the tabs' preferred widths no longer fit;
it runs on tab bar resize, title change and close. The button opens a menu holding one scroll area of
`TabListRow` widgets (a single `QWidgetAction`, at most 360 px or half the screen high, so the menu opens
below the button and never covers it), rebuilt each time it opens (the `+` and list buttons are styled in `Style.cpp` as `#newTab` and `#tabList`, with the chevron drawn as the menu indicator, so the count and arrow never overlap): the current tab is highlighted and scrolled into view, and a click switches to it. A row only reacts
to a press that started on it: otherwise releasing the button that opened the menu would pick a row.

### Settings panel

Built in the `MainWindow` constructor inside a `QScrollArea` (object name `settingsScroll`), in
sections: Appearance, Layout, Data, Help.

- **Theme** is a `ThemeRow` (colour dots and the theme name); clicking it opens the same picker
  as `Alt+T`. It repaints on `ThemeManager::changed`.
- **Font size** is a `−` / `+` stepper over 8, 9, 10, 11, 12, 13, 14, 16, 18, 20 pt. It steps from
  the size in use (`AppFont::effectiveSize()`), so it also works when following the desktop default.
- **Font weight** is a `Segmented` control: Default, Light, Normal, Bold. A saved Medium or
  Semi-bold weight (from the older drop-down) stays in effect but shows no segment selected.
- **FontPreview** uses `AppFont::mono()` as its widget font, so `AppFont` keeps it up to date like
  any other monospace widget, and reads colours from the theme.
- **Side by side** and **Wrap long lines** are `Switch`es bound both ways to the View menu actions
  (`Ctrl+\`, `Alt+Z`), so menu, shortcut and panel always agree. Wrap is now saved as `wrap` in
  `sift.ini` (new key, default off); older files without it load as before.
- **Data** shows the history count (set in `rebuildHistory`), Open folder (`openHistoryFolder`, also
  used by the File menu). There is deliberately no Clear button here (too destructive for a
  settings page); Clear lives only in the File menu ("Clear history…", with a confirm dialog).
- **Reset font to defaults** resets size and weight only. It does not touch the theme or toggles.
- The custom controls paint with palette and `ThemeManager` colours, so they follow every theme;
  the stepper and link buttons are styled in `Style.cpp` (`#stepper`, `#linkButton`, `#quietLink`).

### History panel

`MainWindow` builds the panel: a title row ("History 142", or "History 38 of 142" when
filtered), the filter box, a row of quick-filter chips (All, Errors, GET,
POST, Today) and the list.

- The list is a flat `QTreeWidget` with one column, so keyboard navigation and the
  `topLevelItem(i)->isHidden()` filter logic keep working. Each item carries its data in the
  `HistoryRole` roles (`HistoryList.h`) and `Qt::UserRole` still holds the entry index.
- `HistoryDelegate` paints the whole row itself: method, host with a muted path, a status chip,
  and a small second line (`2m ago · 340ms · 3 params`, plus the reason for errors and redirects).
  The row background (hover, selected) is still drawn by the application stylesheet through
  `PE_PanelItemViewItem`; the selected row also gets a 2 px accent bar. Colours are read while
  painting (`ThemeManager::colors()`, `methodColor()`), so a theme change only needs a repaint.
- Day headings ("Today", "Yesterday", `d MMM yyyy`) are drawn inside the first visible row of each
  day, not as separate items. `applyHistoryFilter()` recomputes `HistGroup` on every filter change,
  so the heading always sits on the first row that is actually shown. Rows are therefore not uniform
  height (`setUniformRowHeights(false)`); `doItemsLayout()` is called after a filter change.
- Clicking a history item (or pressing Enter on it) loads it into the current tab only when that tab
  is blank (`RequestTab::isBlank()`: no URL, params, headers or body). If the current tab already has
  anything in it, the item opens in a new tab so nothing typed is overwritten. Shift or Ctrl always
  forces a new tab. A click opens through `itemClicked` and Enter through `HistoryTree::enterPressed`;
  `itemActivated` is deliberately not connected, because on single-click desktops (KDE) a click emits
  both signals and the item would open twice.
- The `x` button appears on the hovered row and deletes that one history file
  (`HistoryStore::remove`, which refuses paths outside the history folder). `HistoryTree` swallows
  that click so the entry is not opened.
- Relative times ("now", "2m ago", "1h 4m ago") are only used for today's entries; other days show
  the clock time. `HistoryTree::tick` refreshes them once a minute, and the timer runs only while
  the list is visible.
- The URL query is not shown in the row (the tooltip has the full URL); it is summarised as a
  param count. The filter still searches the full URL, method, status and reason.

### Hover count tooltip (Pretty view)

Hovering a JSON key whose value is an array or object shows a tooltip: `N items` for
arrays, `N keys` for objects (`1 item` / `1 key`, `0` for `[]` and `{}`). Only top-level
children are counted. It lives in `FoldingEdit`:

- `containerCountAt(pos, &block, &isArray)` finds the key under the mouse. It parses the
  pretty-printed text itself: a line `"key": [` / `{` (or `[]` / `{}`), with the mouse over
  the key including its quotes, not over the rest of the line. Items are the lines
  exactly one indent level (4 spaces, as written by `prettyJson`) deeper that do not begin
  with `}` or `]`. This depends on `prettyJson` output; if its indent width changes, change it here.
- The viewport has mouse tracking. `mouseMoveEvent` sets `m_hoverBlock` (drawn as a soft
  band across that line in `paintEvent`) and starts `m_tipTimer`.
- The tooltip opens after 120 ms, not Qt's default of about 700 ms. The default `ToolTip`
  event is swallowed in `viewportEvent` so Qt's slower one never fires. Closing is
  untouched (Qt's own hide behaviour).
- `clearHover()` runs on leave, scroll, drag and `resetFolds`.

### Response panel details

- **Tabs.** The response tabs reuse `RequestTabWidget` / `CountTabBar` and the same underline style as the
  request tabs (`#reqTabs, #respTabs` share rules in `Style.cpp`). Request tab indexes: Preview 0, Params 1, Headers 2, Body 3 (the counts and the Body dot use them). `CountTabBar` supports counts on
  up to 4 tabs; Headers shows the number of header lines (`setCount(2, ...)` in `onFinished`).
- **Headers tab (`HeadersPane`).** A strip with `Table | Raw`, a filter box, a `Groups` toggle and a copy
  button, over a stack of two views. *Table* is a `QTextBrowser` filled with generated HTML (a table: names
  proper-cased and in the key colour, values wrapped, rows separated by hairlines). Groups are Content,
  Caching, Security, Cookies, Other (`groupOf`); each `Set-Cookie` is split (newline or `, ` before
  `name=`, never inside an `expires=` date) into `name=value` plus attribute pills. *Raw* is the existing
  read-only viewer with the headers exactly as the server sent them (original case), so Ctrl+F, wrap and
  zoom work there. Ctrl+F in the Table view focuses the filter instead (`RequestTab::openSearch`);
  `currentViewer()` returns null for it. Ctrl+click on a row copies its value (`copyRow`); the copy button
  and Ctrl+Shift+C copy the raw text in both modes. The HTML is rebuilt on theme change (`refresh`).
  Ctrl+wheel zoom works in both views, limited to 6-36 pt (`HeadersPane::eventFilter`; Qt's own zoom has no limit).
- **Indent guides.** `FoldingEdit::paintGuides` draws a faint line every 4 columns of a line's leading
  spaces. Like the item counts it relies on `prettyJson` indenting by 4.
- **Item counts.** The root `[` / `{` shows `N items` / `N keys` after it; a collapsed block's pill
  shows `… N`. Both use `countChildren()`, cached in `m_counts` (cleared by `resetFolds`).
- **JSON path.** `computeFolds()` also builds `m_nodes` / `m_nodeOf` (one node per element line with
  its parent and a segment such as `[3]`, `.key` or `["odd key"]`) in one pass, so a path is built by
  walking parents, with no scanning on hover. `reportPath()` runs on mouse move, on cursor move and
  once a scroll has stopped, and calls the callback set by `RequestTab` (updates `PathBar`). After a
  scroll a resting mouse is over a different line, so `pathUnderMouse()` restarts a 120 ms timer on
  every scroll step: the path is only looked up once the view has stopped, and until then
  `PathBar::setBusy(true)` leaves the bar blank. A closing `}` reports its container's
  path. `PathBar` is display only: not clickable (the hovered line would change on the way to it) and
  with no hint text, so the whole strip is for the path; the right end shows only a short "Copied"
  after a copy. How to copy is told in the F1 list. It shows only on Pretty with JSON
  (`updatePathBar()`).
- **Ctrl+click copies a line's path.** `FoldingEdit::mousePressEvent` takes a Ctrl+left-click, finds
  the line from the click position (so nothing can change under the mouse first), reports its path,
  and calls the copy callback (`RequestTab` puts it on the clipboard and flashes "Copied" in the
  bar). It does not start a selection. A plain click still selects as before.
- **Copy shortcuts.** `Ctrl+Shift+C` copies the whole current response view (Render copies the raw text),
  `Ctrl+Alt+C` copies the path shown in the bar. Both are in the View menu and the F1 list; Ctrl+click
  on a line is in the F1 list too.
- **Momentum and pixel scrolling.** `QPlainTextEdit` can only start drawing at a whole line, so a
  glide through it moves one whole line (about 18 px) per step and looks like a low frame rate at the end. `FoldingEdit`
  therefore draws the text itself in `paintEvent` (a copy of Qt's own block loop using
  `getPaintContext()`, so selection and search highlights still work), shifted up by `m_shift`
  pixels (always less than one line). `scrollPixels()` / `setScrollPixels()` expose the position in
  pixels; the scroll bar still holds whole lines, and anything else that scrolls (keys, search,
  dragging) resets the shift to 0. All code that maps between text and viewport coordinates
  (gutter, hover, guides, pills, hit tests) must use `origin()` and add `m_shift` to a position
  before `cursorForPosition()`. The base class also picks the rectangle to repaint after a selection
  or cursor change from its whole-line offset; with a sub-line offset that rectangle is a few pixels
  off and only a strip of the new selection was redrawn (a double click showed thin bars instead of
  a highlighted word). So while `m_shift` is not 0, `selectionChanged`, `cursorPositionChanged` and
  `updateRequest` repaint the whole viewport. Pretty, Raw and Headers are all `FoldingEdit` (folding is simply
  not enabled for the last two).
  `KineticScroll` (in `ResponseWidgets.cpp`, on Pretty, Raw, Headers and Render) filters wheel
  events on the viewport: it follows touchpad deltas (pixel deltas, or angle deltas below 120),
  tracks the finger speed, and 55 ms after the last event glides on with exponential friction
  (`kTau` 0.7 s, starts above 220 px/s, ends below 20 px/s, capped at 10000 px/s). It stops
  wherever the glide runs out, between two lines like any smooth-scrolling view; there is no
  snapping to a line. Because the text can rest at a sub-line offset, `FoldingEdit` passes every
  mouse press, move, release and double click to the base class through `shifted()` (position moved
  by `m_shift`), so selection and clicks hit the line that is drawn under the pointer. Scrolling by
  anything other than the wheel (keys, search, drag past the edge) resets the offset to 0. Mouse-wheel notches (120) and
  `Ctrl`+wheel zoom are passed through untouched, and platform momentum events
  (`Qt::ScrollMomentum`) are swallowed so it never doubles up. To tune the feel, change the
  constants at the top of that part of the file.
- **Scrollbars** are 5 px at rest and 7 px under the pointer (handle margins in `Style.cpp`).

### Selecting collapsed lines

A collapsed block is drawn as `… ]` after the opening line, but its hidden lines are still
in the document. For the highlight, the copied text and the marker to agree:

- `paintEvent` fills the selection colour over the `…` marker and the closer when the
  selection runs past the end of a collapsed line, and draws the marker in selected colours.
- `mouseMoveEvent` extends a drag that reaches the end of a collapsed line to the end of
  its closing line, in both directions: forwards by moving the cursor position, backwards
  (right to left, or upwards) by moving the anchor. Copying a collapsed line therefore
  copies the whole block.

## Themes

`ThemePicker` previews a theme while you move through the list or type. Applying a theme
restyles every visible widget (palette plus stylesheet; hidden tabs are styled when shown), so the picker waits 90 ms for
a pause in typing or arrow keys before applying (`m_previewTimer`). Do not call
`ThemeManager::apply` from a per-keystroke handler. `accept()` and `reject()` stop the timer
first so a late preview cannot override the final choice.

## Preview tab

The Preview tab has two views, cURL first (the one shown by default) and then HTTP. A strip on top holds a pill-shaped `cURL | HTTP` switch (two checkable `QToolButton`s in a rounded frame, styled locally) on the left and a copy icon (`IconButton`, tooltip "Copy (C)", a "Copied" tooltip after use) on the far right; the views sit in a `QStackedWidget` below it.

`RequestPreview` is an ordinary widget placed as the first request tab (`Ctrl+1`; the tabs are Preview, Params, Headers, Body and the shortcuts follow that order). It is
refilled by `RequestTab::refreshPreview()` when the tab is opened and whenever the URL or
method changes while it is open. With an empty URL it stays blank. Its `C` shortcut uses
`Qt::WidgetWithChildrenShortcut`, so it only works while focus is inside the Preview tab and
never steals the letter from the URL bar or editors. The first call to build the raw HTTP text
probes the system for default headers, which can take up to 1.5 s once.

## Keyboard shortcuts

Shortcuts are defined in `MainWindow` (menu actions) and `RequestTab` (tab-local
`QShortcut`s). When adding one:

1. Check it is not already used (an ambiguous shortcut makes Qt disable both and log
   `Ambiguous shortcut overload`).
2. Add it to the View/File menu or the right `QShortcut`.
3. Add it to the F1 list in `MainWindow` (`showShortcuts` rows).
4. Update the tooltip of the control if it has one.

Current map: `Ctrl+Enter` send, `Esc` cancel the running request or close search, `Ctrl+L` URL, `Ctrl+K`
history filter, `Ctrl+T` / `Ctrl+W` tabs, `Ctrl+Tab` switch tab, `Ctrl+1..4` request
tabs, `Alt+1..4` response tabs, `Ctrl+F` search, `Ctrl+B` sidebar, `Ctrl+\` layout,
`Alt+Z` wrap, `Ctrl+Shift+C` copy response, `Ctrl+Alt+C` copy JSON path of the last line pointed at, `Ctrl+click` a line in Pretty copy its path, `Alt+T` themes, `Ctrl+M` menu mode, `F1` help, `Ctrl+Q` quit. In the Preview
tab, `C` copies the visible view.

## How to make common changes

### Add a request tab or response view

1. Create the widget in the `RequestTab` constructor and `addTab` it.
2. If it can be zoomed, add it to the zoom clamp.
3. If it should only exist sometimes, use `setTabVisible` (see the Render tab) and guard
   `showResponseView`.
4. Add a shortcut and an F1 row.
5. Make sure `currentViewer()` and `updateSearch()` still behave (they handle
   non-`QPlainTextEdit` pages by doing nothing).

### Add a body type

Extend `kBodyTypes`, the body combo, the body stack, `requestJson`/`loadRequest` (so it is
saved and restored), `buildRequest` (so it is sent), and `CurlParser` (so import works).
Keep old saved files loading: unknown or missing values must fall back to "none".

### Add an HTTP method

Add it to the method combo, `methodColor()` (otherwise it gets the error colour), and the
`known` list in `CurlParser`.

### Add a Settings option

Add a control to the Settings page in `MainWindow` (follow the `field()` helper so the label,
control and one-line hint keep the same spacing), store it with `QSettings` (`"sift"`,
`"sift"`, INI format), apply it at startup in `main.cpp`, and add it to "Reset to defaults".

### Add a response pane state

See "Which response page is shown" above: one page, one flag or badge kind, one line in
`setBadge`.

### Add a column or behaviour to the key/value tables

Params, Headers and the Form body all use `KeyValueTable`, so a change there applies to all
three. Editing starts on a single click (`AllEditTriggers`).

### Add a sidebar panel

Add a page to the `m_panels` stack, add a rail button with `SideRail::addButton` and a
matching `Icon` painted in `RailButton::paintEvent`, and switch the stack when it is clicked.

### Change the look

Edit `Style.cpp` (see "Styling and theming"). Then test at least one dark theme, one light
theme and the KDE system theme.

### Change what is saved

History and session files are user data. Add new fields, never rename or remove existing
ones, and read missing fields with a default so files written by an older build still load.

## Debugging

- **Run it from a terminal** so Qt warnings are visible. Useful ones: `Ambiguous shortcut
  overload`, `QSortFilterProxy`/style sheet parse warnings (a bad rule is silently
  ignored, so if a style does not apply, look for a typo in `Style.cpp`).
- **Check the files directly.** History, session and settings are plain text. Delete
  `session.json` to start with a clean tab set. Delete `~/.cache/sift` to regenerate icons.
- **Slow or frozen UI** usually means a huge document being laid out (zoom, highlighting
  of a large response) or a repeated full restyle. Check the zoom clamp, the 2 MB cap, and
  how often `Style::install` runs.
- **Colours wrong after a theme change** means a widget cached a colour. Connect it to
  `ThemeManager::changed` and recompute (see `RequestTab::onThemeChanged`).
- **Highlight missing or doubled** means two highlighters on one document. See "Response
  views".
- **A widget ignores the font setting** means it was given a literal font. Use
  `AppFont::mono()`.
- **Crash in the constructor** is often an event filter or slot running before a member is
  created (this happened with `m_reqTabs`). Initialise pointer members to `nullptr` and guard
  filters.
- **A response page does not show or does not clear** means the badge kind or `m_cancelled` is
  not what you think. Follow `setBadge` (see "Which response page is shown").
- **Theme switching feels slow, or a widget keeps the old theme's colours,** see "Traps with the stylesheet" (deferred hidden widgets). Slowness means something calls `Style::install` or `ThemeManager::apply`
  too often. See "Themes".
- **A state widget keeps animating when hidden** means its timer is not tied to
  `showEvent`/`hideEvent` (see `Spinner`).

## Testing

There is no test target. During development, behaviour was verified with small throwaway
programs that link the project's object files and run offscreen:

```bash
# object files from the existing build, without main.cpp
OBJS=$(ls build/CMakeFiles/sift.dir/src/*.o | grep -v main.cpp.o)
MOC=build/CMakeFiles/sift.dir/sift_autogen/mocs_compilation.cpp.o
g++ -std=c++20 -fPIC test.cpp $OBJS $MOC -Isrc \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Network Qt6Test) -o test
QT_QPA_PLATFORM=offscreen ./test
```

Tips:

- Render with `widget->grab().save("out.png")` and look at the picture.
- Use a throwaway local HTTP server (for example `python3 -m http.server`) for responses.
- Point `XDG_CONFIG_HOME` at a temporary directory so tests do not change your real
  settings.
- The offscreen platform cannot grab the keyboard and has no real window focus, so send
  key events directly to the widget (or to `windowHandle()` for popups).
- `QTest::mouseClick(QWidget*)` bypasses popup routing. Use the `QWindow*` overload for
  popups.
- Offscreen runs cannot show Wayland-specific behaviour (popup placement, focus). Check
  those on the real desktop.
- Remember to relink a test program after rebuilding the project; a stale test binary shows
  the old behaviour and looks like a bug.
- For states that need a slow server (Pending, Cancelled), use a small local server that
  sleeps a few seconds before replying.

## Updating and releasing

There is no auto-update, by design. The project is not under version control yet, so
put it in a git repository (`git init`) before making larger changes; that gives you
history and an easy way to undo. To update, change the source and rebuild:

```bash
cmake --build build
```

Before shipping a change, run through this checklist:

1. Builds with no new warnings.
2. Works in a dark theme, a light theme, and the KDE system theme.
3. Works with a different font size and weight in Settings, and after "Reset to defaults".
4. Ctrl+scroll zoom still stops at 6 and 36 pt in every view (including Render and Preview).
5. Old history and session files still load.
   Also check the tab bar (badge, hover close, muted new tab, overflow list with many tabs) and the Settings panel (stepper, weight, switches stay in sync with the menu) and the History panel (rows, day headings, chips, hover `x`) and the Pretty view: hover tooltip and band on array and object keys, and selecting a
   collapsed line from both ends highlights `… ]` too.
6. Any new shortcut appears in the F1 list and does not collide.
7. Nothing new blocks the UI thread. Network work is asynchronous, and timers run only while
   their widget is visible.
8. No new dependency, no telemetry, nothing written outside the paths in "Where data lives".

## Rules to keep in mind when changing things

- Respect the philosophy in `ABOUT.md`: native, fast, small, local, keyboard-first, and
  every feature must save steps. If a change adds weight without saving steps, do not add it.
- One change at a time. Explain a large change before making it.
- Never delete or migrate a user's files silently.
- Keep behaviour that users rely on stable: shortcuts, file formats, key order in the
  Pretty view, the "never record cancelled requests" rule.
- Prefer small, focused classes in their own files over growing `RequestTab.cpp`, which is
  already the largest file.
- Keep comments for the non-obvious "why" (most of the traps above exist because of a real
  bug that took time to find).

## Known limitations

- Folding in the Pretty view is mouse-only.
- Touchpad momentum and sub-line resting exist only for wheel/touchpad scrolling; keys, search jumps and
  drag-to-scroll move by whole lines. Mouse-wheel notches are not smoothed.
- `Ctrl+Alt+C` copies the path last shown in the bar (the last line the mouse or cursor was on);
  `Ctrl+click` on a line is the precise way.
- The hover tooltip only works on the Pretty view's pretty-printed JSON (not Raw), only on keyed
  arrays and objects, and not on arrays inside arrays (they have no key).
- The Render tab cannot show modern layout (flexbox, grid) or run scripts, by design.
- Tabs that scroll (more tabs than fit) do so with the trackpad or wheel; Qt's scroll arrows are
  hidden (`QTabBar::scroller { width: 0px }` in `Style.cpp`) and the overflow list is the other way
  to reach a hidden tab.
- **HTML highlighting.** `HtmlHighlighter` also colours the bodies of `<script>` (JavaScript: keywords,
  literals, strings, numbers, comments, function calls, regex literals, template literals over several
  lines) and `<style>` (CSS: selectors, at-rules, properties, values, colours / numbers, `!important`,
  comments, nesting depth). The multi-line state rides in the block state (`state + kind*10 + sub*100`).
  It is a tokenizer, not a parser, so odd code can be coloured wrongly, but never changes the text. It costs
  about 0.2 s per 100 KB. Only the Pretty tab is coloured; the Raw tab is deliberately plain text (no
  highlighter is ever attached to its document). The Headers tab's own Raw view still colours name / value.
- Ctrl+F with text selected in Pretty / Raw / Headers-Raw fills the search box with it (up to 2000 characters) and jumps to the first match (`RequestTab::openSearch`). Line breaks and tabs of a multi-line selection are written as `\n` and `\t` (a real backslash as `\\`), as HTTPie shows them; `updateSearch` reads those escapes back, and searches the plain text with `QString::indexOf` (case-insensitive) because `QTextDocument::find` cannot match across lines. So a copied block of lines, indentation included, finds itself. A match over several lines is drawn as one merged outline (`FoldingEdit::matchRects` walks the blocks).
- **Search match highlight.** `FoldingEdit` paints the matches itself (`setMatches`, `paintMatches`), not as Qt extra selections: every visible match gets a rounded fill (radius 4, the theme's accent `QPalette::Highlight` at .5 alpha on dark themes, .25 on light) *under* the text, so syntax colours stay readable; the current match gets a lighter fill plus a 2.5px ring (`kRing`; HTTPie's 2px looked thinner on our smaller font) and a 10px glow in the same accent *over* it. Sizes and alphas are copied from HTTPie's own CSS (`.cm-searchMatch`, `.cm-searchMatch-selected`); the colour is the theme's, never a fixed blue. A box is the match plus 2px each side and the font height plus 4px, not the whole line. Rectangles come from the text layout line by line (`matchRects`) and are merged into one shape (`matchShape`), so a match that wraps over several lines gets a single outline, ring and glow instead of overlapping boxes, and translucent fills do not double up. (More line spacing would not help: `QPlainTextEdit`'s document layout ignores `QTextBlockFormat::lineHeight`, tried.) Drawing everything in `paintEvent` also avoids the stale pieces of old highlights that Qt's own repaint rectangles left inside long wrapped lines. `RequestTab::paintMatches` hands the ranges over.
- Tab counts exist only for the request Params / Headers tabs and the response Headers tab. The request Body tab shows a dot instead (there is nothing to count): when the type is JSON / Raw and the text is not blank, or Form with at least one ticked row; never for None (`refreshRequestCounts`).
- The Headers table has no hover copy icon (rich text cannot show one); copying a single value is Ctrl+click.
- The path bar uses JavaScript-style paths (`[0].address.city`); there is no JSONPath (`$`) or Python form.
- A theme switch takes about 0.4 s with 60 tabs open (visible widgets are restyled at once, hidden
  tabs when first shown, about 100 ms each). It is quick with a few tabs.
- Raw, Headers, Pretty, Render and Preview zoom independently.
- HTTP only; HEAD, multipart upload and cURL file-upload flags are not supported and the
  cURL importer reports them.
