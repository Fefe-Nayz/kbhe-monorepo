# KBHE/libhmk RGB bridge v1

The bridge uses one 64-byte RAW HID report in each direction. Byte 0 is the
command, byte 1 is reserved in requests and is the status in responses, and the
payload starts at byte 2. Multi-byte integers are little-endian. RGB values are
always logical `R,G,B`; the firmware owns the physical WS2812 `G,R,B` encoding.

USB discovery must distinguish the three images sharing VID `0x9172`:

| Image | PID | RAW HID interface | Usage page / usage |
| --- | ---: | ---: | --- |
| Native application | `0x0002` | 1 | `0xFF00 / 0x0001` |
| Signed native updater | `0x0003` | — | not an RGB target |
| Optional libhmk | `0x0004` | 2 | `0xFFAB / 0x00AB` |

The libhmk image retains its upstream keyboard interface 0, generic HID
interface 1 and optional XInput interface 3. Keeping libhmk's `FFAB:00AB`
collection preserves hmkconf compatibility. Hosts should filter PID and usage,
then negotiate command `0x7F`; interface numbers alone are not a stable API.

Status `0` means success, `1` is a device/storage error, and `3` is an invalid
parameter. A host must negotiate `GET_RGB_CAPABILITIES` before sending writes.

## Discovery (`0x7F`)

The response payload is:

| Byte | Field |
| ---: | --- |
| 2 | Protocol major (`1`) |
| 3 | Protocol minor (`0`) |
| 4 | LED count (`82` on KBHE 75HE) |
| 5 | Bytes per pixel (`3`) |
| 6 | Maximum frame chunk (`60`) |
| 7 | Runtime live effect ID (`7`) |
| 8–9 | Capability bitmap |
| 10 | Color order (`0` = logical RGB) |

Capability bits 0 through 6 respectively mean enabled state, brightness,
individual pixel, frame chunks, fill, live mode, and restore mode.

Portable effect IDs are:

| ID | Effect | Persistence |
| ---: | --- | --- |
| `0` | Static base color | persistent |
| `1` | Breathing base color | persistent |
| `2` | Rainbow | persistent |
| `3` | Rainbow wave | persistent |
| `7` | Host-controlled live frame | runtime only |

An implementation may add effects outside this portable set, but a host must
not assume that private IDs have the same meaning on native KBHE and libhmk.

## Commands

| ID | Command | Request payload | Response payload |
| ---: | --- | --- | --- |
| `0x60` | Get enabled | — | enabled |
| `0x61` | Set enabled | enabled | enabled |
| `0x62` | Get brightness | — | brightness |
| `0x63` | Set brightness | brightness | brightness |
| `0x64` | Get pixel | index | index, R, G, B |
| `0x65` | Set pixel | index, R, G, B | index, R, G, B |
| `0x68` | Get frame chunk | chunk index | chunk index, length, up to 60 bytes |
| `0x6A` | Set frame chunk | chunk index, length, up to 60 bytes | chunk index, length |
| `0x6B` | Clear | — | — |
| `0x6C` | Fill | R, G, B | R, G, B |
| `0x6E` | Get effect | — | effect |
| `0x6F` | Set effect | effect | effect |
| `0x76` | Restore previous effect | — | effect |
| `0x7F` | Get capabilities | — | discovery descriptor above |

For an 82-LED frame, chunks 0–3 contain 60 bytes and chunk 4 contains 6 bytes.
Chunk 0 begins a new transaction and clears the received bitmap. Every chunk
must use its canonical index and length. The frame is published only when the
complete bitmap is present; merely receiving the numerically final chunk is not
enough. Publication closes the transaction, so a late non-zero chunk cannot
resubmit or combine frames. Changing effect, setting a direct pixel, filling,
or clearing cancels any partial transaction.

Pixel and chunk writes require live effect 7. A fill in an autonomous mode
updates its persistent base color; a fill in live mode is a runtime frame
operation. To request a persistent solid color portably, select effect 0 and
then send `FILL`. Live frames and effect 7 never write Flash.
