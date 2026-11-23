# Phase 0: governance and spec alignment

| Deliverable | Where | Status |
|---|---|---|
| Canonical tablet evdev contract | `linux` `8828595e7ee6`, `Documentation/input/tablet-guidelines.rst` | Committed and pushed to the dev branch. Docs build with 0 warnings. Needs human Signed-off-by, then an RFC to linux-input. See `kernel-submission.md` |
| `tablet-v2` version 3 draft | `wayland-protocols` `65c2bebdb6e8`, `stable/tablet/tablet-v2.xml` | Committed and pushed. `wayland-scanner --strict` clean, meson 197/197. Needs Signed-off-by, then an MR. See `tablet-v2-v3.md` |
| XDG tablet profile spec | `libtablet` `b2e1af15`, `xdg-tablet/` | Committed and pushed. Schema tests 32/32, libtablet suite 18/18. See `xdg-tablet-profiles.md` |
| Vendor-neutral namespace | `governance.md`, `freedesktop-project-request.md`, `vendor-invitation.md` | Drafts only. Nothing sent or requested |

## How the pieces fit

* The kernel doc defines what every evdev node looks like. libinput and libtablet depend on that and need no vendor quirks.
* `tablet-v2` v3 adds what compositors need to expose to apps:
  * standalone remotes, with no fake tablet
  * the output a pen display is mapped to
  * label feedback routed to OSD or OLEDs
  * whether a pressure curve is applied
* The profile spec is the user-configuration side. A compositor applying a profile's pressure curve reports
  `zwp_tablet_tool_v2.pressure_curve = custom` (v3).

## Human-only steps, in order

1. Review all three commits and add `Signed-off-by` (`git commit --amend -s`). AI assistants must not add it
   (kernel `Documentation/process/coding-assistants.rst`). wayland-protocols CI enforces it.
2. Talk to the linuxwacom maintainers about `governance.md` before anything public.
3. Send the kernel RFC (`kernel-submission.md`). Open the wayland-protocols MR (`tablet-v2-v3.md`).
   Request review of the profile spec (`xdg-tablet-profiles.md`).
4. File the freedesktop.org group request, then send the vendor invitations.

## Cross-cutting open questions

These appear in more than one deliverable.

* **Dial direction.** The kernel doc proposes clockwise is positive. libinput negates `REL_WHEEL` but not `REL_HWHEEL`, so dial 1 and dial 2 currently disagree. Settle it with hardware recordings.
* **Standalone pads in libtablet.** `IntegratedIn=Remote` also covers the Wacom ExpressKey Remote, which pairs with a tablet. A new `.tablet` key is needed to tell compositors when to send `standalone`.
* **Mode LED naming.** userspace hard-codes `::wacom-`. Decide on a vendor-neutral name before Phase 1 adds LEDs for other vendors.
* **Touch arbitration mode names** must match between the profile spec and the future libinput API.
* **Pads with more than 18 buttons** (kernel numbering) or more than 26 buttons (libwacom uses a `char` per button).
