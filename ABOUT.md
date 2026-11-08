# sift

A small, fast, native API client for Linux. Built with C++20 and Qt 6 Widgets.
No accounts, no cloud, no telemetry, no browser engine, no scripting runtime.

## What it is

sift sends an HTTP request and shows you the response, nothing more and nothing less.
You type or paste a request, press `Ctrl+Enter`, and read the result. Every request
you send is kept as a plain file on your disk, so you can find it again.

It is a personal replacement for HTTPie Desktop and Postman, made for one specific
workflow: poking at web endpoints while writing scrapers.

## Why it exists

The existing options each fail the same way:

- **Postman, Insomnia, HTTPie Desktop** are Electron apps. They are slow to start,
  heavy on memory, want you to sign in, sync your data to a cloud, and ship telemetry.
  They are web pages pretending to be applications, so they look and behave out of
  place on a KDE desktop.
- **The native Linux options** I looked at were either unfinished or had pulled in
  large dependency stacks (Python, JavaScript engines, web views) that put them back
  in the "bloated" category.
- **curl in a terminal** is fast and honest, but reading a large JSON response,
  folding it, searching it, re-running a request from last week, or editing headers
  in a table is painful.

sift fills the gap: the speed and honesty of curl with the comfort of a GUI client,
and nothing else attached.

## The problem it solves

When building a scraper you spend your day doing the same loop:

1. Take a request from the browser's network tab ("Copy as cURL").
2. Run it, look at what came back.
3. Tweak a header, a parameter, or the body. Run it again.
4. Compare with the previous attempt. Come back to it tomorrow.

sift is shaped around that loop:

- **Paste a cURL command into the URL bar** and the whole request (method, URL,
  headers, body, query parameters) is filled in for you.
- **Preview** shows the exact request that will be sent, as raw HTTP and as cURL,
  so you can copy it back out.
- **Responses** are pretty-printed, syntax-highlighted, foldable, searchable, and
  also available raw, as headers, and (for HTML) rendered.
- **History** is kept automatically and is filterable with `Ctrl+K`.
- **Tabs** are independent (each has its own cookies) and survive a restart.
- **Cancel** a slow request with `Esc`.

## Philosophy

These are the rules every change is judged against.

1. **Native or nothing.** C++ and Qt Widgets only. No QML, no web views, no embedded
   JavaScript, no Python, no Electron. It should look and feel like a KDE application
   because it is one.
2. **Fast.** It starts instantly and never blocks the interface. Networking is
   asynchronous. Anything that could take noticeable time is limited or deferred
   (large responses skip highlighting, zoom is clamped, theme previews wait for a
   pause in typing).
3. **Small.** Few dependencies: Qt Widgets and Qt Network. Adding a dependency needs a
   very good reason.
4. **Your data is yours.** Everything saved is a plain, readable file on your own
   disk. No account, no cloud, no telemetry, no auto-update, no phoning home. The
   only network traffic is the requests you send.
5. **Keyboard first.** Every common action has a shortcut. The mouse works, but you
   should never need it. `F1` lists every shortcut.
6. **Every feature must save steps.** If something does not make the request loop
   faster or clearer, it does not belong. Features are added one at a time, on
   request, and not speculatively.
7. **Honest by default.** The preview shows what is really sent. The render view does
   not run scripts and does not fetch anything unless it is an image or stylesheet
   needed to display the page. Limits (size caps, file caps) are stated, not hidden.
8. **Looks finished.** Consistent spacing, one accent colour, quiet chrome, themes
   that actually apply everywhere. Rough edges are bugs.

## What it deliberately does not do

- No sync, no sharing, no teams, no cloud collections.
- No scripting, no test runners, no environments with variables (yet).
- No GraphQL, WebSocket, gRPC or other protocols. HTTP only.
- No JavaScript execution in the HTML render view. It is a readable preview, not a
  browser, and modern layout (flexbox, grid) is not supported by Qt's rich-text engine.
- No automatic updates. You update it by pulling and rebuilding.

## Feature summary

| Area | What you get |
| --- | --- |
| Requests | GET, POST, PUT, PATCH, DELETE; params, headers and body (JSON, form, raw) |
| cURL | Paste a cURL command into the URL bar to import it; copy the request back as cURL |
| Preview | The exact HTTP request and cURL equivalent, with `C` to copy |
| Response | Pretty (folding, server key order kept), Raw, Headers, and a Render view for HTML |
| Search | `Ctrl+F` in the response, with match navigation |
| History | Automatic, one file per request, instant filter |
| Tabs | Independent cookie jars, restored on restart, side-by-side or stacked layout |
| Themes | Your KDE theme, or any NvChad (base46) theme, with a live picker |
| Settings | App-wide font size and weight |
| Keyboard | Everything, listed on `F1` |

## Who it is for

One person, on KDE Linux, who builds scrapers and wants an API client that gets out
of the way. If it also suits you, good, but its direction is set by that one workflow.
