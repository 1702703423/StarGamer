# Star Gamer AXF display test

This Keil ARMCLANG project produces a relocatable AXF test application for the
runtime loader in the main Star Gamer firmware.

## Build and run

1. Open `Star Gamer ELF.uvprojx` and build `Target 1` with F7.
2. Copy `Objects/Star Gamer ELF.axf` to the root of the SD card.
3. Rebuild and flash the main firmware, then open `AXF Loader` from the main
   menu and select the AXF file.
4. PB1, PA2, PC1, and PA1 increment their corresponding on-screen counters.
   PB7 exits the AXF application and returns LCD ownership to LVGL.

The application allocates a 320x240 RGB565 framebuffer as part of its loaded
ZI region. The host loader places that region in external SRAM; no DMA buffer
is placed in CCM.

Run the host compatibility check after each AXF rebuild:

```powershell
python ..\Tests\AXF_Loader_Host\validate_axf.py
```
