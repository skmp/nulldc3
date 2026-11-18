# refsw3

The PowerVR2 as a console does it, measured on hardware by dreamcast-tests: `refta/` is refsw3-ta (the tile
accelerator), `refcore/` is refsw3-core (the ISP and TSP, in `namespace refcore` beside `libswirl/rend/refsw2`). In the
emulator the renderer chooses the TA (`libswirl/hw/pvr/ta_lle.h`): `refsw3` is refsw3-core with refsw3-ta, `refsw2`
the old core with refsw3-ta, `gles` (legacy GL) the emulator's own TA. `../refsw3-offline` is refsw3 without the
emulator, the program dreamcast-tests renders with.
