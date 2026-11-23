# Phase 0: XDG tablet profile specification

Draft: libtablet `claude/elegant-goldberg-5odfs6`, commit `264f1abd`, directory `xdg-tablet/`:

- `profiles.md` – the specification (schema-version 1)
- `profile.schema.json` – JSON Schema (draft 2020-12) for the parsed TOML
- `example-profiles.toml` – pen display, opaque tablet, standalone remote with OLED/dial, Krita/Blender/GIMP overrides
- `test/test_xdg_tablet_profiles.py` – schema and semantic validation, run by `meson test` (32 cases)

## Rationale

- Each desktop stores tablet settings in its own place (GNOME gsettings
  `org.gnome.desktop.peripherals.tablet*`, KDE `kcminputrc`, Sway config
  `input` blocks). Artists lose their configuration when they switch, and
  minimal compositors have no settings UI at all.
- The format reuses existing semantics instead of inventing new ones:
  - device match strings are libwacom `DeviceMatch` (`bus|vid|pid|name|uniq`);
  - `area` is libinput's normalized rectangle;
  - `pressure-range` is libinput's tool pressure range;
  - `pressure-curve` is the four-number Bézier GNOME already stores (scaled
    0..1 instead of 0..100);
  - output mapping uses the same EDID triple as GNOME;
  - pad buttons use libwacom letters (as GNOME's per-button schemas do).
  GNOME and KDE can therefore migrate losslessly in either direction.
- `passthrough` is the default for every control, so apps that use
  tablet-v2 pad events directly (Krita, Blender) keep working, and
  `set_feedback` labels from the app reach OSDs and OLEDs.
- Compositors applying a non-identity curve must announce it with the
  tablet-v2 v3 pressure-curve event so apps don't apply a second curve.
- Multi-writer safety is a flock on `profiles.toml.lock` plus a re-read, then
  temp file, fsync, rename and directory fsync. Readers need no lock and
  reload through inotify.

## Review request (draft)

> **Subject:** RFC: XDG tablet profile specification (profiles.toml), draft 1
>
> We'd like feedback on a desktop-neutral file format for pen tablet and
> pen display configuration: pressure curves, area, output mapping,
> pad/ring/strip/dial bindings, radial menus, OLED labels and per-app
> profiles. It is meant to be read by compositors and written by any
> settings tool, so an artist's configuration survives a change of desktop.
> Everything maps 1:1 onto libinput configuration and the existing GNOME
> settings. The draft and a JSON Schema are at <libtablet MR link>.
>
> We'd particularly value input from:
> - **GNOME:** Mutter (input settings, `MetaInputSettings`) and GNOME Settings
>   (Wacom panel). Topics: migration from gsettings, whether Mutter would read
>   this file directly or keep gsettings as the source of truth with
>   import/export.
> - **KDE:** KWin input and the Plasma drawing-tablet KCM. Topics: mapping of
>   KCM features (pen button rebinding, per-app profiles), `kcminputrc`
>   migration.
> - **wlroots/Sway:** whether a minimal compositor would load this file, or a
>   separate `tabletctl` daemon should apply it through existing Sway `input`
>   commands.
> - **Cosmic** (cosmic-comp, cosmic-settings): adoption as their native format.
> - **libinput:** that the semantics match the config API, especially the new
>   arbitration modes.
>
> Please reply on the MR or on wayland-devel.

Send after the libtablet namespace exists, so the MR link is stable.

## Open questions

1. Should GNOME and KDE treat the file as the source of truth, or as an
   import/export format next to gsettings and `kcminputrc`? This decides
   whether compositors write it.
2. Should calibration results written by the compositor (interactive "tap to
   identify" mapping) go to `$XDG_STATE_HOME` instead of `profiles.toml`?
3. Per-unit settings for several identical devices (phys path? serial?).
4. More than 26 pad buttons: the libwacom API names buttons with a single
   `char`.
5. `exec:` actions (security, sandboxing) and key sequences.
6. `app-id` normalisation for Flatpak and X11 clients, and whether globs are
   allowed.
7. Final names of the touch arbitration modes. They must match whatever the
   libinput API change ends up calling them.
8. Xencelabs Quick Keys USB IDs in the example (28bd:5202/5203) must be
   confirmed when the libtablet definition is added.
