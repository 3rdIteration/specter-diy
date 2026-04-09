# Specter Shield Lite (Work in Progress)

![](shield_lite_image.jpg)

## Background
Limited availability and high cost have limited the accesibility of the Smartcard based functionality in Specter.

## The Specter Shield-Lite aims to:
+ Implement the QR and Smartcard functionality of the Specter Shield, at a price that is comparable to using the Waveshare QR Scanner for Specter DIY.
+ Preserve the form factor of the Shield to allow re-use of existing cases.
+ Optimise the PCB design and component selection for low cost, short-run production where full hand-assembly is also practical with a standard soldering iron if desired. 
+ Not worry about an integrated battery, but expose the VIN pin so that the user can supply 6-9DC if they want, or just power over USB.
+ Use FOSS design tools (KiCad) to make it easier for others to contribute to the project

## Design Decisions to keep costs low
+ PCB Size - Exceeding 10cm in either dimension typically results in increased costs with most PCB production houses
+ 2 Layer PCB - Double sided PCBs are extremly cheap when compared to 4 layer boards. This is also a fairly simple board, so 2 layer is fine.
+ Keeping all drilled hole sizes above 0.3mm... The existing Shield has a handful (~6) of 0.2mm holes which dramatically increases the production cost.
+ Single Sided Components - Fab houses like JLCPCB have extremely cheap assembly services as long as all components are on a single side of the PCB. (The Pin-Headers are easy to hand solder on)
+ Sticking to common/cheap components where possible, as opposed to more exotic/complex parts. (Minimise 'Extended' parts from fabs like JLCPCB, prefer pin-count <= 16 for fabs like PCBWay)

## Smartcard Interface

The original Shield used an ST8034 IC for the smartcard interface, but availability
and cost have been persistent problems. Rather than swapping to a different IC package
(e.g. ST8034ATDT), the recommended approach is now to use a **discrete basic
components design** that eliminates the smartcard IC entirely.

See [`shield/smartcard_minimal/`](../shield/smartcard_minimal/) for the full
schematic and BOM — 11 JLCPCB basic parts (< $0.35), all 0805/SOT-23, no
firmware changes required. This directly addresses the Shield Lite's core goals:

+ **Cost** — discrete BOM is cheaper than any ST8034 variant
+ **Availability** — all parts are JLCPCB basic library, always in stock
+ **Hand-solderability** — 0805 resistors/caps + SOT-23 transistors are easy with a standard iron
+ **Assembly cost** — all components are ≤ 3-pin SOT-23 or 0805, cheap to assemble at any fab (JLCPCB, PCBWay, etc.)
+ **Short-circuit protection** — built-in ~88mA current limiter via sense resistor + NPN clamp

## Future Work

+ Look at alternative QR Scanners like the Grow GM805 which are cheaper, remove the need for fine pitch header soldering and also integrate the beeper, to further drive costs down. (Currently has some compatibility issues with binary SeedQR which need to be explored/fixed)