# Kernel: tablet/pen-display evdev contract, submission notes

Commit: `8828595e7ee6` on `linux` branch `claude/elegant-goldberg-5odfs6` (based on v7.4-rc4).
Files: `Documentation/input/tablet-guidelines.rst` (new) and `Documentation/input/input_uapi.rst` (toctree).

## Verified

* `make SPHINXDIRS=input htmldocs`: builds with 0 warnings and 0 errors.
* `scripts/checkpatch.pl --strict -g HEAD`: the only error is the missing `Signed-off-by`, which is intentional (see below).
  The warnings are about the `Co-Authored-By` tag spelling and whether MAINTAINERS needs an entry. The
  existing `INPUT` entry already covers `Documentation/input/`, so no MAINTAINERS change is needed.
* Claims checked against code: `hid-input.c` (DIRECT/POINTER from the application collection, tilt
  converted to radians), `wacom_wac.c` (prop selection, pad button numbering, placeholder pad X/Y,
  `ABS_MISC`, `touch_arbitration`, LED names), `libinput` (`evdev-tablet-pad*.c` ring, strip and dial
  handling, the `::wacom-` LED path), and systemd `udev-builtin-input_id.c` (pad identification, joystick range).

## Before sending (human steps)

1. Review the text and take responsibility for it. Per `Documentation/process/coding-assistants.rst`,
   only a human may add `Signed-off-by`:
   `git commit --amend -s`
2. Disclose tool use per `Documentation/process/generated-content.rst`. Either keep the
   `Co-Authored-By` trailer, or replace it with a sentence in the changelog or cover letter
   naming the assistant used.
3. Send it as an **RFC**. It proposes conventions (dial direction, strip encoding, LED naming) that
   the HID and Wacom maintainers need to agree on:

```sh
git format-patch -1 --rfc --cover-letter -o outgoing/ 8828595e7ee6
git send-email \
  --to="Dmitry Torokhov <dmitry.torokhov@gmail.com>" \
  --to="Jiri Kosina <jikos@kernel.org>" --to="Benjamin Tissoires <bentiss@kernel.org>" \
  --cc="Jason Gerecke <jason.gerecke@wacom.com>" --cc="Ping Cheng <ping.cheng@wacom.com>" \
  --cc="Jonathan Corbet <corbet@lwn.net>" \
  --cc=linux-input@vger.kernel.org --cc=linux-doc@vger.kernel.org \
  outgoing/*
```

(Recipients come from `scripts/get_maintainer.pl` for the doc plus `hid-input.c`, `hid_bpf`,
`wacom` and `hid-uclogic`. Also consider Cc'ing the libinput maintainer, since libinput is the primary consumer.)

## Cover letter draft

> **[RFC PATCH 0/1] Documentation: input: graphics tablet and pen display guidelines**
>
> Tablets reach userspace through three kernel paths: hid-generic plus HID-BPF,
> wacom, and hid-uclogic. They do not produce the same evdev topology, and
> libinput, libwacom and the udev hwdb carry per-vendor workarounds as a result.
> This RFC writes down the interface all three should converge on. It mostly
> codifies what wacom already does, so that new HID-BPF programs and hid-uclogic
> have a written target and new devices need zero userspace quirks.
>
> Points I'd like feedback on:
>
> 1. **Dial direction.** No convention exists today. The draft says clockwise
>    is positive on `REL_WHEEL(_HI_RES)`. Note that libinput currently negates
>    `REL_WHEEL` for dial 1 but not `REL_HWHEEL` for dial 2. Hardware recordings
>    of the Huion Keydial, XP-Pen ACK05 and Surface Dial would settle the
>    convention.
> 2. **Strip encoding.** wacom reports `1 << position`. The draft allows that
>    and asks new drivers to report a linear position, with 0 reserved for "no
>    finger". libinput already handles both forms (it uses log2 for wacom's vendor ID only).
> 3. **Mode LED names.** userspace hard-codes `<input>::wacom-<group>.<mode>`.
>    The draft keeps that name for every vendor for now. Is a vendor-neutral
>    name, plus a compatibility path in libinput, preferable before other
>    drivers start registering LEDs?
> 4. **Pads with more than 18 buttons.** wacom's numbering stops at
>    `BTN_BASE2`. Should we extend it with `BTN_TRIGGER_HAPPY*`, or something else?
> 5. **Placeholder `ABS_X`/`ABS_Y` on pads.** This is required by udev's
>    `input_id` today. Should we codify it (as the draft does) or change udev?
> 6. **Touch arbitration.** The draft says new drivers should leave pen/touch
>    arbitration to userspace. wacom's `touch_arbitration=1` default is kept
>    for compatibility.
