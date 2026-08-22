# License notice

The maintained [KBHE libhmk fork](https://github.com/Fefedu973/libhmk) and the
offline mirror `patches/libhmk-kbhe-rgb.patch` modify
[upstream libhmk](https://github.com/peppapighs/libhmk), which is licensed under
GPL-3.0. The fork, patch, and resulting alternative firmware are distributed
under GPL-3.0 as well. The native KBHE firmware does not link or copy libhmk;
it only implements the independently documented RAW HID interoperability
protocol in `PROTOCOL.md`.

The reviewed [hmkconf](https://github.com/peppapighs/hmkconf) project is also
GPL-3.0. This integration does not vendor or modify hmkconf.

The optional image has the distinct USB identity `9172:0004`. The native
application (`9172:0002`) and signed updater (`9172:0003`) remain separate
images and are not derivative works of libhmk. The libhmk image must not be
distributed as, or installed through, the native signed-updater workflow.
