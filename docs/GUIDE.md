# Calendar - user guide

## Installing

Calendar is a desk accessory: TOS loads it at boot from the root of the
boot drive, and it waits in the Desk menu until you choose it.

| Your Atari        | Copy to `C:\`    | Connects                        |
|-------------------|------------------|---------------------------------|
| Falcon, TT        | `CALENDAR.ACC`   | by itself (Falcon mode) or through the Pi |
| ST, STE, Mega ST  | `CALST.ACC`      | through the Raspberry Pi gateway |

Calendar keeps its settings and the copies of your calendars in the folder
`CALENDAR` on the boot drive (`C:\CALENDAR\`), made the first time it runs:

```
C:\CALENDAR.ACC
C:\CALENDAR\CALENDAR.INF    settings, accounts, calendars
C:\CALENDAR\CALENDAR.KEY    the key the passwords in CALENDAR.INF are encrypted with
C:\CALENDAR\CACHE\          a copy of every calendar
C:\CALENDAR\CACERT.PEM      Falcon mode: root certificates (copy them here)
C:\CALENDAR\ROOTS.DAT       Falcon mode: the same, decoded ahead of time
```

`CALENDAR.PRG` is the same program, to start from the desktop or a
shell; started that way it keeps its files in its own folder and ends when
its window closes.

**Memory.** The accessory takes its working memory once, at boot (an
accessory can't safely take more later): 1.5 MB on a Falcon or TT,
256 KB on an ST, never more than half of what's free. A 1 MB ST runs it;
with big calendars, 2 MB or more is better. `heapkb=` in CALENDAR.INF
sets another amount, in kilobytes.

**The clock.** Calendar's "today" is the Atari's clock, so set it (an ST
without a clock card starts each day at an old date; the Control Panel or
a clock program at boot sets it). In Settings, tell Calendar which time
zone the clock is in.

## Connecting

Open **Connection** from the menu (the ≡ button at the right of the
toolbar).

- **Raspberry Pi gateway**: the Pi's address and port (8180). Set the Pi up
  with `gateway/install.sh`; see [gateway/README.md](../gateway/README.md).
  Any Atari can use it, and a Falcon can too, to save time.
- **Falcon mode**: Calendar makes the secure connections itself. Needs a
  68030 (Falcon or TT), `CALENDAR.ACC`, and `CACERT.PEM` and `ROOTS.DAT` in
  `C:\CALENDAR\`. **Check certificates on the DSP** lets the Falcon's
  DSP56001 do the RSA arithmetic (much faster than the 68030).
- A server on your home network that speaks plain `http://` (a Radicale or
  Nextcloud without TLS) needs neither.
- **Keep on this Atari**: how many days back and ahead Calendar keeps
  (92 and 400 to begin with). Fewer use less memory and sync faster.
- **Write a log**: `CALENDAR.LOG` in `C:\CALENDAR\`, the conversation with
  the servers (passwords left out) when something doesn't work.

## Adding calendars

The ≡ menu > **Add account or calendar...**:

- **iCloud**: your Apple ID and an **app-specific password**. Make one at
  [appleid.apple.com](https://appleid.apple.com) > Sign-In and Security >
  App-Specific Passwords; your normal password won't work. Calendar finds
  all your iCloud calendars, with their colours.
- **Other CalDAV account**: the server's address and your login, e.g.
  `https://caldav.fastmail.com/`, `https://cloud.example.org/remote.php/dav`
  (Nextcloud), `https://caldav.calendar.yahoo.com`. The address of one
  calendar works too.
- **Subscribe to a published calendar**: a `webcal://` or `https://`
  address of an `.ics` file: public holidays, a sports team, a school, or a
  Google calendar (Google's CalDAV needs an OAuth sign-in Calendar can't do,
  but every Google calendar has a "Secret address in iCal format" in its
  settings). Subscriptions are read-only and refresh on their own schedule.
- **On My Atari**: a calendar kept only on this computer. Right-click it
  to import an `.ics` file into it.

The calendars appear on the left, under their accounts. Click one to show
or hide it; double-click for its name, colour and whether its alerts
show; right-click for more (show only this one, refresh, import,
unsubscribe). Right-click an account to refresh it, change its settings or
delete it from the Atari (nothing is deleted on the server).

## The window

![Week view](week.png)

The toolbar: **‹ Today ›** move back, to today and forward; then the
period's name (double-click it to go to a date); **Day Week Month Year**;
**+** a new event; **Refresh** (with Shift: everything again, from
scratch); **≡** the menu.

- **Month**: each day with its events; all-day and multi-day events as
  bars across the days, the others one line each, "+2 more" when they
  don't fit (click it for the day). Days of other months are grey.
- **Week** and **Day**: hours down the side, all-day events at the top,
  overlapping events side by side. Scroll with the arrows or the slider;
  a red line marks the time now. The day view shows the selected event's
  details on the right when the window is wide enough.
- **Year**: twelve months, days with events in bold. Click a month's name
  for that month, double-click a day for the day.

Today is in red everywhere. The info line under the title shows when
Calendar last updated, the selected event, or what it's doing.

Click an event to select it, double-click to open it. Double-click an
empty day or hour for a new event there. Right-click an event to open,
duplicate or delete it; right-click a day for a new event or to change
the view.

**Hebrew dates** (≡ > Hebrew dates) show next to each day: the day of the
Hebrew month in each cell, the month's name on its first day, the full
date in day and week views.

## Events

![New event](new-event.png)

- **Title**, **Location**, **Notes** take Hebrew: press **F10** for the
  Israeli keyboard (in every text field, as in EMail), F10 again for
  English. Text is laid out right to left when it starts in Hebrew.
- **Starts / Ends**: dates as day.month.year (`12.10.2026`, `12.10`,
  `12/10/26`), times as `9:30`, `930` or `9:30pm`.
- **all-day** for birthdays, holidays, trips.
- **Calendar**: which calendar it goes in (only those you can change).
- **Zone** (with time zone support on): the event's time zone; "Floating"
  for events that happen at the same clock time anywhere.
- **Repeat**: every day, weekday (Sunday to Thursday when the week starts
  on Sunday, Monday to Friday otherwise), week, two weeks, month, year,
  **every year on the Hebrew date** (birthdays, yahrzeits: 12 Tishri every
  year, whatever the civil date), or **Custom...**. The line
  under it says in words what the repeat is ("Every month on the last
  Friday"). **End repeat**: never, on a date, or after a number of times.
- **Custom...**: how often (every *n* days, weeks, months, years, hours,
  minutes), which weekdays, "on the [first/second/third/fourth/last]
  [day/weekday/weekend day/Sunday...]" of the month or year, days of the
  month (`1,15,-1` for the 1st, the 15th and the last), months, hours and
  minutes. At the bottom is the rule itself in iCalendar's words
  (`FREQ=MONTHLY;BYDAY=-1FR`), which you can also type into: anything
  RFC 5545 allows works there (`FREQ=YEARLY;BYMONTH=3;BYDAY=TH`,
  `FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1`,
  `FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO`...).
- **Alert**: when to remind you. All-day events have "on the day (9:00)"
  and "1 day before (9:00)".

Changing or deleting an occurrence of a repeating event asks whether it's
**only this one** or **this and all future ones**, as on the Mac:

- **Only this one**: that occurrence becomes an exception with its own
  time, title, place or notes; the rest of the series stays. Moved again
  later, it stays one exception. Deleted, it's left out of the series.
- **All future ones**: the series ends before this occurrence and a new
  one starts here with the changes. On the first occurrence, that's the
  whole series.
- Changing the repeat itself applies from the chosen occurrence on.

Events from iPhones, Macs, Outlook and Google with extra dates, excluded
dates or rules, or "this and future" changes show as they should.

Calendar keeps what it doesn't edit: invitations' attendees, other
programs' settings, attachments, exceptions made on an iPhone all stay as
they were when it saves an event.

**Find** (^F) looks through titles, places and notes from a year back to
two years ahead.

## Reminders

When an event's alert comes due, Calendar beeps and shows it over
whatever program is running, even with its window closed. **Show** opens
the day. Settings > **Show alerts** turns them all off; each calendar has
its own switch too (subscriptions start with theirs off).

## Time zones

Settings:

- **Atari clock in**: the zone the Atari's clock is set to. Calendar needs
  it to know what time it is everywhere else.
- **Time zone support**: each event shows and edits its own zone, and
  **Show times in** lets you see the calendar as it looks from somewhere
  else, e.g. before a trip. Off, everything is shown in the clock's zone.

Calendar knows the summer time rules of about 90 zones (Israel's, Europe's,
the US and Canada's, Australia's and New Zealand's, Chile, Egypt, Lebanon,
Cuba...) and learns others from the calendar data (Outlook's zones, for
instance).

## Keys

| Key                 | Does                                    |
|---------------------|-----------------------------------------|
| T, Clr/Home         | today                                   |
| D W M Y             | day, week, month, year view             |
| ← →                 | the day before/after (Shift: the period) |
| ↑ ↓                 | a week up/down (month); scroll the hours (day, week) |
| Tab, Shift+Tab      | the next/previous event                 |
| Return              | open the selected event (none: a new one) |
| Delete              | delete the selected event               |
| Esc                 | nothing selected                        |
| N, ^N               | new event                               |
| ^F                  | find                                    |
| ^G                  | go to a date                            |
| ^R                  | refresh (Shift+^R: everything again)    |
| ^S                  | show/hide the calendar list             |
| ^W, ^Q              | close the window                        |
| F10                 | Hebrew keyboard                         |
| Help                | the keys                                |

## Which version

The ≡ menu > **About Calendar** shows the version, e.g. `1.1.6`, and when it was
built. The last number goes up with every build.

## When something goes wrong

- **"needs a secure connection"**: set up the Pi gateway, or Falcon mode.
- **"Can't reach the gateway"**: is the Pi on, and is `calgate` running?
  Open `http://PI-ADDRESS:8180/` in any browser: it should answer. Use the
  Pi's IP address, not its name.
- **"refused the user name or password"**: iCloud wants an app-specific
  password, not your Apple ID's.
- **"certificate is out of date (or the Atari's clock is wrong)"**: in
  Falcon mode the clock must be right to check certificates.
- An **!** next to an account in the sidebar: its last refresh failed; the
  calendar's info (double-click) shows why.
- `CALENDAR.LOG` (Connection > Write a log) has the details.
