# Upstream components

This repository contains the integration layer. Bootstrap fetches upstream code
at the revisions in `dependencies.lock.json`; runtime libraries are pinned by
Specter's gitlinks. Those components retain their original copyrights/licenses.

- Specter-DIY: cryptoadvance, MIT, https://github.com/cryptoadvance/specter-diy
- ForgeBox example/drivers: YANSSIE HK LIMITED and original driver authors,
  see `vendor/forgebox/LICENSE` and individual source headers,
  https://github.com/KeystoneHQ/forgebox-helloworld
- f469-disco runtime/integration: original authors,
  https://github.com/diybitcoinhardware/f469-disco
- MicroPython: Damien P. George and contributors, MIT; the runtime carries
  additional component notices (including Mbed TLS's Apache-2.0 notice).
- LVGL and generated bindings: original LVGL/MicroPython binding authors, MIT.
- embit: Stepan Snigirev and contributors, MIT.
- secp256k1-zkp: Bitcoin Core/Elements contributors, MIT.
- FreeRTOS: its upstream copyright holders, MIT; see vendor source headers.
- Boot splash: [Specter logo](https://specter.solutions/assets/Specter_logo_1756046218246-Cuyd3_u2.png).
  The logo is cropped to its alpha bounds, scaled to 384×406, and composited
  over black. The logo and labels are stored as big-endian RGB565 for the LCD.

`tools/build.py` copies the original LVGL configuration and startup assembly
into the generated build tree. It removes an unused retail event notification
from the generated SD-card driver and exports the startup reset symbol. The
upstream checkouts themselves are not patched.

`port/camera.c` adapts the official ForgeBox `drv_qrdecode.c` pin/clock setup and
links the pinned vendor `MH1903_QRDecodeLib.a`. That proprietary precompiled
component remains subject to its vendor terms and MCU license check; the port's
license does not relicense it. The capture/preview/transport integration is new.
