# tablet-v2 version 3: rationale and merge-request draft

Branch: `sda-mikerochefort/wayland-protocols` `claude/elegant-goldberg-5odfs6`. The commit is
`tablet-v2: add version 3 with standalone pads, output mapping and pressure curve state`, and it
changes only `stable/tablet/tablet-v2.xml`.

Validation (Fedora 45, wayland-scanner 1.26.0):

- `wayland-scanner --strict` client-header / server-header / private-code: ok
- `xmllint --noout`: ok
- `meson test` (wayland-protocols 1.50 tree): 197 Ok, 0 Fail. This includes the scan, pedantic-C and C++
  build tests for `tablet-v2`, and `cursor-shape-v1`, which depends on it.
- Generated wire signatures: `enter` = `u?oo` (tablet now nullable), `standalone` = `3`,
  `output` = `3?o`, `pressure_curve` = `3u`.

Before submitting upstream, the author must add a `Signed-off-by:` line. The repository's CI runs
`ci-fairy check-commits --signed-off-by`.

---

## MR description (draft)

**tablet-v2: version 3 — standalone pads, output mapping, pressure curve state**

### Problem

1. **Stand-alone pads.** Shortcut remotes and dial controllers without a digitizer of their
   own (Xencelabs Quick Keys, XP-Pen ACK05, Huion Keydial K20, and similar) show up as tablet pads. However,
   `zwp_tablet_pad_v2.enter` requires a non-null `zwp_tablet_v2`. Compositors therefore either
   invent a fake tablet for the pad or do not expose the device at all. Clients cannot tell a pad
   that belongs to a real tablet from one that belongs to a fabricated one. Some clients also delay
   pad binding until a `tablet_added` arrives.
2. **Output mapping is invisible to clients.** Compositors map a pen display to its own
   screen, or switch it to "whole desktop" mode. Clients have no way to learn which output that is,
   so palettes, radial menus and tool panels cannot be placed on the pen display automatically.
3. **Double-applied pressure curves.** Compositors and settings panels now offer pressure
   curves and pressure-range remapping, as do drawing applications (Krita, GIMP, Blender). Neither
   side knows about the other, so users end up with two curves applied in series.
4. **set_feedback output targets are unspecified.** Pad hardware increasingly has its own
   displays (OLED button and dial labels). The protocol only mentions on-screen displays and
   does not say how long a description stays valid.

### Changes

All interfaces are bumped to version 3. As with version 2, the objects depend on each other,
so every interface is bumped. All additions are `since="3"`.

| Interface | Addition | Notes |
|---|---|---|
| `zwp_tablet_pad_v2` | event `standalone` | At most once, before `done`. Immutable for the object's lifetime. |
| `zwp_tablet_pad_v2.enter` | `tablet` arg `allow-null="true"` | Null **iff** the pad is standalone, and **never** for clients bound below v3. Older clients get the previous behaviour: a compositor-chosen tablet, or the pad is not announced. |
| `zwp_tablet_v2` | event `output` (nullable `wl_output`) | Sent before `done` if mapped, and again on every change. Null means not mapped to exactly one output (whole desktop, spanning, relative mode), or that the client hasn't bound the matching `wl_output`. It is re-sent when the client binds the matching output, and once per resource if the client binds it more than once. On `wl_output` global removal, a new event (null or the new output) is sent; its order relative to `global_remove` is not guaranteed. |
| `zwp_tablet_tool_v2` | enum `pressure_curve` {`linear`, `custom`} + event `pressure_curve` | State, not per-frame. Sent before `done` and on change. A change applies from the next `frame` boundary. `linear` still allows hardware compensation (tip-offset subtraction). A range remap or a non-linear curve is `custom`. The curve itself is not exposed. |
| `set_feedback` (pad, ring, strip, dial) | Documentation only | Compositors *may* present descriptions on screen and on hardware displays. Defines when a description stops applying: replaced by a new `set_feedback` for the same feature and serial, the next `mode_switch` for its group, or `zwp_tablet_pad_v2.leave`. Clients should keep descriptions short. |

The top-level protocol description also gains a paragraph introducing standalone pads.

### Compatibility analysis

- **Wire compatibility.** The only change to an existing message is nullability on
  `zwp_tablet_pad_v2.enter`'s `tablet` argument. libwayland's marshaller rejects NULL for
  non-nullable objects, so a compositor must be built against the v3 XML to send null at all.
  The description forbids sending null below v3. A client built against older XML can bind
  at most v2, so it never receives a null tablet. A client built against v3 XML that binds v2
  sees the same behaviour as before.
- **Old clients on a v3 compositor:** unchanged. No new events are sent below v3, and enter
  never carries a null tablet.
- **New clients on a v2 compositor:** no `standalone`, `output` or `pressure_curve` events
  arrive. The specification states the "not sent → unknown" interpretation explicitly for
  `output` and `pressure_curve`.
- **set_feedback documentation:** the new text is permissive ("may") and consistent with existing
  behaviour. Clients already re-send feedback after `mode_switch`, which compositors send after
  every `enter`.

### Open design questions for review

1. **Nullable argument vs. new event.** Making an existing argument nullable in a minor version is
   the least invasive option, but it is less common in this repository. The alternative is a
   separate `enter_standalone(serial, surface)` event. Reviewers may prefer one or the other. The
   wire behaviour is equivalent.
2. **`output` as object vs. name.** An object argument follows `wl_surface.enter`, but it
   depends on which globals the client has bound. That is why null can mean "unbound", and why a
   multi-bind case exists. The alternative is a string matching `wl_output.name` (v4+), which
   avoids binding state entirely but requires `wl_output` v4 in clients.
3. **Partial-output mappings.** An active area mapped to a *sub-rectangle* of one output is
   reported as that output. Whether a region (in output-logical coordinates) should be exposed is
   left out deliberately. Feedback is welcome.
4. **Granularity of `pressure_curve`.** Two values are enough to avoid double curves. Should
   compositor-side minimum-pressure thresholds (libinput's pressure range) count as
   `custom`? The draft says yes for range remaps and no for automatic tip-offset compensation.
5. **Hardware-display size hints.** There is no protocol-level hint about label length or
   glyph budget on hardware displays. A future addition could advertise a per-feature
   `feedback_hint(max_chars)`.
6. **Pairing policy.** A remote paired with one specific tablet (sold as a bundle, sharing a
   receiver) is non-standalone if the compositor associates it. This is compositor policy and is
   deliberately not encoded.

### Governance requirements (GOVERNANCE.md §2.2 / §2.3)

- **Process.** An amendment to an existing protocol uses the normal MR process, with no minimum
  discussion period (§2.3.5).
- **ACKs.** The protocol is in the legacy `zwp`/`wp` namespace. It needs ACKs from at least
  3 member projects, NACKs from none, and an in-depth review from at least one member project
  other than the author (§2.2.2, §2.2.3, §2.2.8). The obvious reviewers are GTK/Mutter
  (Carlos Garnacho maintains tablet support), KWin, wlroots/Sway, Qt and Weston.
- **Implementations.** Three open-source implementations are required: either 1 client and 2
  servers, or 2 clients and 1 server (§2.2.4). In this project's scope (Mutter as the only
  compositor), that means **Mutter plus two clients**, for example **GTK4** and **Qt6** (or
  SDL3/Krita). Pursuing alternate compositors would allow the alternative split:
  Mutter + KWin/wlroots + one client.
- **CI.** Commits need `Signed-off-by`.

### Implementation checklist (for this project)

- [ ] Mutter: send `standalone` and a null `enter.tablet` for libinput pad devices with no paired
      tablet (needs libinput/libtablet to report "standalone"); send `output` from the existing
      output-mapping code; send `pressure_curve` based on the configured
      curve and range settings.
- [ ] GTK4: expose standalone pads (no `GdkDevice` tablet association), the output hint, and the
      pressure-curve flag. Wire feedback strings.
- [ ] Qt6 (QtWaylandClient `QTabletEvent` / `QPointingDevice`): same.
- [ ] libinput/libtablet: expose "standalone". libtablet's `IntegratedIn=Remote`
      (`WACOM_DEVICE_INTEGRATED_REMOTE`, used by e.g. Huion Keydial K20/KD100 and the Wacom
      ExpressKey Remote) is **not** sufficient on its own: the EK Remote pairs with a tablet. A
      `.tablet` key that distinguishes stand-alone from paired remotes is needed.

### Out-of-scope issue noticed

The existing `set_feedback` descriptions on the strip, pad and dial interfaces say "associated
with this ring" (a copy-paste error). That is worth a separate trivial cleanup MR.
