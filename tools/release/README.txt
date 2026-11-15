E2 Control: Kindle Click firmware @VERSION@
==========================================

WARNING: flashing this firmware can permanently damage the "E2 Control"
device. There is no recovery tool. Use it at your own risk.

This download does NOT contain a ready-to-flash image. It contains the
compiled app (app.bin). You put it into the firmware image of your own
unit, which you download from the vendor.

It is for the HANLINYUE E2 Control with the Jieli AC632N chip only.
Do not use it on any other remote.

What it does
------------
The remote presents itself as Amazon's Kindle Click, so a stock Kindle
pairs with it. The Kindle lists it as a "Kindle Click".

                                Page mode       Brightness mode
  Next / previous, short press  turn the page   one step brighter / dimmer
  Next / previous, held         nothing         keeps changing
  Third button, short press     to brightness   to page mode
  Third button, long press      Kindle sleeps   Kindle sleeps
  Third button, held 28 s       forgets the pairing

The remote starts in page mode. Brightness mode ends 15 s after the
last press; the LED is steady on while it lasts.

What was tested
---------------
The sources of this version ran on one unit (stock variant
"zoom-phone-tablet") with a Kindle Colorsoft, software 5.20.1.0.2.
This file was built by GitHub Actions from those sources. The build
itself has not run on a device.

What you need
-------------
- A Mac with Python 3 and hidapi (brew install hidapi). The flash tool
  e2boot.py is written for macOS and is untested elsewhere.
- The vendor image of your unit's variant, e2control-<variant>.bin.
  The project README says where to download the images:
  https://github.com/felipe-dos-santos81/e2control-bt-kindle-page-turner

Steps
-----
Run these in the unzipped folder, with the E2 on a USB cable.

1. Find the serial port:
     ls /dev/cu.usbmodem*
2. Put the remote in update mode:
     python3 -I e2boot.py enter /dev/cu.usbmodemXXXX --yes
3. Find your variant. Run this for each vendor image:
     python3 -I e2boot.py verify e2control-<variant>.bin
   On a remote with its stock firmware, your variant is the image where
   all blocks match, or all but one. Keep that image: it is your only
   way back. Stop here if no image matches.
4. Put the app into your image:
     python3 -I jlimg.py pack e2control-<variant>.bin app.bin e2-kindle.bin
5. Flash and restart:
     python3 -I e2boot.py flash e2-kindle.bin --yes
     python3 -I e2boot.py reboot --yes
6. Pair the remote on the Kindle.

To go back to the stock firmware, do steps 1, 2 and 5 with your vendor
image in place of e2-kindle.bin.

Known limits
------------
- The remote never switches itself off, so the battery drains while it
  is on. Switching it off and waking it up are untested.
- The Kindle is told a fixed battery level of 88 %.
- The side selector does nothing.
- The test unit once stopped starting after it was switched off, and
  came back by itself five days later. The cause is unknown.

Licence
-------
The project's own code is MIT licensed (LICENSE). app.bin also contains
code from Jieli's fw-AC63_BT_SDK (Apache 2.0, LICENSE.jieli-sdk.txt).
The vendor's firmware is not included and remains the property of its
rights holders. This project is not affiliated with Amazon, HANLINYUE
or Jieli.
