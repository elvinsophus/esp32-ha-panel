# Product Requirement Document (PRD)

This document preserves the original high-level product vision, UX philosophy, and experiential goals of HAPanel.

It intentionally contains:
- aspirational language
- subjective UX goals
- emotional direction
- conceptual interaction philosophy

For implementation-oriented details, refer to:
- AGENTS.md
- ARCHITECTURE.md
- docs/

# Overall vision

This project aspires to provide an operating system to an ESP32-powered panel, with the most comfortable user experience. It's a product, not a toy nor experiment.

# Non-Goals

This project is NOT intended to become:
- a generic tablet OS
- a web browser wrapper
- a Home Assistant Lovelace clone
- a Linux desktop environment
- a plugin marketplace
- a feature-dense dashboard

# Styling guidelines

I'm not expecting anything beyond the what can be provided or afforded by such scanty resources, but they should also be wisely used to achieve the most comfortable and enjoyable user experience.

Simplistic, modern, and elegant styles should be adopted, which reflect into fonts, icons, colours, and so on. Add some aesthetically advanced details whenever possible, like round corners, gradients, shadows, and so on. I would like a user to perceive the deliberate stylish choices and exquisite endeavour and aspiration behind the system design, instead of a randomly scrambled concoction. It should feel much closer to Ubuntu than a tiny Linux distribution, much closer to Samsung or iPhone than a $20 mobile.

Sans-serif should be the dominant font class. 

Almost-flat (not entirely-flat) icons are preferred. Each icon should have at least a few states: default, pressed, unavailable (hover is unnecessary as we definitely won't need a mouse).

Always display units with numeral values unless absolutely unambiguous. And always prefer the metric system unless conventions dictate otherwise.

# Layer designs

The panel comprises numerous layers (pages), among which the Root page is by default present, from which other pages can be invoked by gestures, or AI interactions.

## Root page

I call it the "root" page (like that of a complex yet balanced tree) instead of "home" page to avoid ambiguity of potentially meaning a page displaying information of _my home_.

This is the ambient layer, like a home page on other operating systems, where not many activities are going on. It should be kept simple, as a welcome page, instead of a dashboard inundated by abundant information.

Conventionally to make it welcoming, this layer should contain a large block displaying the clock (digital or analogue, optionally with seconds), the current date and weekday, and the weather (which may also integrate subtle animations). These should reside at the top half of the screen, with larger text and icon sizes especially the clock, as clear as possible so that to be visible at a distance.

A block at the bottom half (either left or right) of the root page should contain an animated expression representing the presence of the AI. Two eyes would likely suffice for this role, with mutable outlines and preferably pupils. It should reflect the mood within an interaction, have infrequent but not stagnant motions when idle, and be sleeping when dormant or at night (and even exhibit a deadlier form when unavailable).

We should find a tiny spot within the rest of the empty space near the bottom for an icon for a limited selection of pinned actions like switches, scenes, and so on, which makes them only two clicks away from the root page. This selection should be configurable at runtime, as it's highly dynamic and can't afford reflashes of image. It should probably contain links to nearby (one swipe away) pages as well since the user might not always recall which way to swipe.

Three consecutive layers from left, bottom, and right can be invoked with one swipe. We'll omit the one at the top since the status bar also has swiping-down actions which can result in confusions. Here are the three layers, detailed discussions in later sections:

- Left: Home status layer
- Bottom: Security & protection
- Right: Other Apps

Each of these three layer should contain a tiny home icon at the edge indicating and consolidating the spatial relationship between them to the root page. For example the Home status should have such a tiny icon on its right edge as the root page is at its right, and the same goes to the other two respectively. This tiny icon is also clickable whose action is the same as swiping against the direction of the root page, which is to return to it.

The background can have a simple low-contrast image, preferably with subtle motions so that the whole screen looks less boring.

## Interactive AI

This is a non-negotiable must-have, without which the panel becomes simply a classic soulless dull device, instead of a smart and vivid assistant and companion. There must have been mature solutions of a local AI assistant available like the "小智" that the panel is shipped with. We might adopt other options, and probably with hybrid approaches to utilise remote models which are definitely much more intelligent.

This AI shall be awaken by speech in limited forms, also known as "wake-up words", which are configurable. Upon wake-up, if the current layer is the root page its AI block should play an animated expression to indicate they're activated, listening and ready for assistance, otherwise a tinier one should appear from the edge or corner of that layer without blocking any icons within it.

In both voice and text should be what this AI responds, the former via its integrated speaker(s) (optionally via remote speakers as well), the latter at the bottom of the screen, just like subtitles. If the current layer is the root page, the text should be contained in or right next to the expression block, otherwise an oblong modal layer at the bottom should be adopted for display without blocking any icons.

TTS should have multilingual support, at least English and Mandarin, with dynamic auto language switching, as the user speaks fluently both.

Optionally when clicking on the expression block in the root page, it expands to occupy the whole screen, with a dominant expression (in higher resolution), and larger text (both in size and length capacity) at its bottom.

This AI module is not a independent function of this panel. It should be aware of the setup of the panel, and be able to read statuses of and perform actions upon most if not all devices and functions not only on HA but also on this device. For example:

- If the user requests to turn on/off a light or adjust its brightness, the light's card should be invoked to allow visual confirmation and manual operations
- If the user requests to play a piece of music, the music player should be displayed

## Home device statuses

This layer displays the statuses of all IoT devices at home, classified into categories including but not exclusively:

- Lights
- Switches
- Climate
- IoT Devices
- Batteries
- Plants

Each category has an icon within the grid, which when clicked expands to a detailed page. Such a detailed page contains the full list of exposed entities within its category, dynamically organised from the list it queries from HA, each item having its corresponding card type and attribute fields. For example:

- A switch has an on/off button
- A light has a slider for brightness/colour if such functions are available
- A temperature/humidity sensor has numerals displayed but not editable
- A climate device has a few modes as well as temperature adjustment and so on

Since the categories are pre-defined, each detailed page can have its template accustomed to its limited types of entities.

A back-to-previous button should be maintained at a fixed position (or optionally achieved by the gesture of two fingers pinching toward the centre).

## Security & protection

Security and protection related sensors deserve their own layer with their unequivocal significance. Such sensors includes but not exclusively:

- Cameras (if real-time streaming is unaffordable, snapshots are fine as well)
- Doorbell (with a button to unlock the door)
- Door lock
- Smoking sensors
- Gas leakage sensors
- Water leakage sensors

## Other Apps

This layer contains shortcuts to a few other Apps or utils, which are mostly optional but fun to have. Examples (just examples, not meaning I would like to immediately have them) are:

- Media player (probably through a remote speaker)
- Sound visualiser
- Calculator
- News feeds
- Stock/crypto prices
- Mini games

Each App can occupy the whole screen if necessary, with minimise/close buttons at fixed positions, or temporarily hidden but easily found.

## Model layer

This layer is designated to interruptive events, which temporarily stays on top of everything else (but does not necessarily block interactions to the layer it covers) as such an event demands immediate attention. These include but not exclusively:

- AI interaction
- Alarms
- Schedules
- Doorbell rings
- Incoming phone calls
- Other warnings or urgent notifications

This layer often does not expand to the whole screen, but only takes up a tiny space at one edge or corner, so that it does not prevent ongoing interactions. These types of events abovementioned don't necessary share the same layer, to avoid conflicts.

## Status bar

Status bar is omnipresent in almost if not all layers, to display anything significant that requires attention all the time wherever the user goes, in the form of tiny icons, including but not exclusively the following:

- Wi-Fi status (connectivity, signal strength)
- Bluetooth status
- Notifications (info, warning, error: in different colours)
- Clock (when having left the root page)

## Notifications

Swipe down from the left half of the status bar and we get a layer for the notifications, each with an icon on the left indicating its source, a title followed by a short preview of its content in the middle, and a tiny button to expand it a little if the preview is too long. Items are sorted in reverse-chronological order.

## System statuses and quick actions

Swipe down from the right half of the status bar and we get a layer for the system statuses and quick actions. Here reside the most frequently used items, in a grid, including but not exclusively:

- CPU, RAM, etc.
- Wi-Fi status and configuration
- Bluetooth status and configuration
- HA/MQTT connectivity
- Uptime
- Hardware/firmware/software versions and available updates
- Brightness (of the panel, not lights)
- Volume
- Day/night mode
- Other useful shortcuts
- Software/firmware update available

## Keyboard

We can't exclude the scenarios where text inputs are useful if not mandatory, including passwords, searches, and so on.

Input methods are not necessary--we'll just support ASCII characters, including alphabetic letters, digits, and symbols. The layout of the keyboard should feel familiar, preferably QWERTY.

# Transitions & animations

This looks optional but is actually non-negotiable. Such transitions shall appear wherever needed, including but not exclusively layer switching.

Multiple tiers of transitions and animations should be supported and run-time configurable, to accommodate various types of models and screens. There must be some optimised ways to achieve smooth transitions with limited resources.

# Storage management

The panel should be able to persist various kinds of information, including but not exclusively the following:

- Wi-Fi SSID & password
- HA URL and credentials
- Volume
- Brightness
- User preferences

# Device profiles

The project is not meant to serve as specific as one ESP32 model and one panel, while nor does it aspire to be installed on unlimited types of devices. With highly potential switching of devices taken into account, a profile that contains device specific configurations should be maintained so that no further coding shall be required when migrating from one kind of device to another. Such a set of configurations includes but not exclusively the following:

Hardware information:

- Model of ESP32
- CPU class
- RAM/PSRAM
- Other resources
- Panel resolution (width x height)

Software UI (mostly determined by hardware, also with room for user preferences):

- Font class
- Font sizes
- Icon sizes (various sets of the same icons, in sizes like 20/32/48/etc.)
- Status bar size (and therefore status bar icon size)
- Margin sizes
- Scaling (XS/S/M/L/etc., we may be able to unify other size-related configurations into this one)
- Row and column numbers
- Animation tier (none/primitive/smooth/elegant)
- Themes and palettes (so that colours don't scatter everywhere)

Most of these configurations are constant as hardware plays a significant role in their determination, but some values can still rest in a range, either for user preferences, adaptation to scenes, or for convenient testing without recompilations. This would require both storage and adaptive coding.

# HA integration

The panel should be able to interact with Home Assistant (i.e. HA), to which most of this panel's functions are bound, both to send and receive information.

# Character sets

Though English is currently (and in the foreseeable future) the only language of the system, non-ASCII characters should also be able to be displayed, consistently in style with ASCII ones.

# Administration

Some specific configurations shall require administrator privilege to modify or even view, with changeable password(s). But this feature has lower priority.

# Coding guidelines

This is an industry-level code base, not some fun project by some college students.

The user is also an advanced software engineer, who has high and strict standards.

- Always keep the structure cleanly organised
- Always foresee potential changes and take actions to minimise future alterations should one comes
- Be generic whenever possible without dependencies on ephemeral facts
- Never scatter duplicate (magic) variables or code blocks everywhere
- Time and space efficiency are appreciated
- A make-it-work-first-then-refine-it-later-with-iterations approach is allowed as well as practical
