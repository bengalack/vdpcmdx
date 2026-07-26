# vdpcmdx | Measure V9938/V9958 command performance on the MSX

This tool is the successor to [vdpcmd](https://github.com/bengalack/vdpcmd). This time we address the difference between VBLANK AREA and ACTIVE AREA and make it possible to fully automate the tests and write to stdout/files.

Compare your current VDP's command engine performance with real life / target numbers, using this simple __msx dos__ application. Download [here](https://github.com/bengalack/vdpcmdx/raw/refs/heads/master/dska/vdpcmdx.com).

## What it does
It measures the amount of pixels which the command engine is able to "produce" in one full frame, in 192 or 212 line mode, in the 50Hz or 60Hz frequency: Both outside VBLANK and inside ACTIVE AREA, but separated.

**Method:**
* The following is done for VBLANK area (1) and the ACTIVE area (2):
    * We run 5 tests in both landscape and in portrait (10 in total); COPY HMMM, LMMM, YMMM and FILLRECT HMMV, LMMV.
    * The above are run under these conditions:
        * Normal*
        * Sprites off
        * Screen off
        * Normal and VDP is hammered**
        * Screen off and VDP is hammered**

\*(sprites and screen on, the CPU is leaving the VDP alone)  
\**(hammered means the CPU executes continuous unrolled OUT(98),A)

![screenshot](img/v2.0_sanyo_phc-70fd.jpg)
These are the results from Sanyo PHC-70FD in diff mode.

`THIS` columns show the results from the current computer and is measured towards a master value in the `REAL` column. Masterdata comes from Sony HB-F1XD. If run in diff-mode, the `REAL` columns are replaced by `DIFF` columns with shows the diff towards the master - quite useful as it is easier spot discrepancies.

## Help text
![screenshot](img/help_v2.png)

## Understanding the results
For real MSX HW, results in conditions 1-3 will likely not diverge much (normal diff is 0-1, maybe a fluke up to 6). Conditions 4 and 5 are harder to get perfect timings on, and we can allow a diff up to ~250 without anything being wrong. I have seen varying results on the same model.

Regardless of the actuall diffs in condition 4 and 5, the value in 4 should be quite lower than in 1, and value in 5 is assumed to be somewhat lower than in 3.

## Detail section
* If you run this on turbo R or in ("Panasonic") turbo mode, the tool jumps into Z80 3.5MHz during test.
* We currently use screen 8. I have tested other screen modes too. They seem identical in performance when we measure in bytes.
* Portrait/Landscape does not matter much, but they do somewhat, and that is why they are included (but I thought the diff would be bigger).
* To understand why condition 4 and 5 is added (and why we allow bigger diffs here), we must refer to [this research](https://map.grauw.nl/articles/vdp-vram-timing/vdp-timing.html) where we see that both the VDP and the CPU is fighting over the same timeslots/timeline. Microsecond timings matter here and different hardware implementations can make a difference.
* REAL:
    * in the vdpcmd/predecessor tool I measured values on SONY HB-F1XDJ (V9958), PANASONIC FS-A1ST (V9958), SANYO PHC-70FD (V9958), PANASONIC A1-WSX (V9958), PANASONIC FS-A1 (V9938). The were pretty much the same, so going further I stick to using one model, SONY HB-F1XD, as master.
    * From the PANASONIC FS-A1 (V9938), PHILIPS NMS 8245 (V9938) and the SONY HB-F1XD which has 0 extra wait states, the "hammered" command costs 18 cycles, while on the others it costs 19 cycles. I can see from my numbers that this affects the amount of pixels in condition 4. The more CPU commands, the less pixels produced by command engine on COPY commands.
* I have tried hammering with both READ and WRITE. They seem identical in performance.

### The actual VDP commands (WIP)

The short side is 40 pixels (l=40, h=0)
The long side is 256 pixels (l=0, h=1)

| Orientation | Command | `sxl, sxh, syl, syh, wl, wh, hl, hh, color, arg` | Log Op |
| --- | --- | --- | --- |
| HORIZONTAL  | HMMM | `sxl, sxh, syl, syh, wl, wh, hl, hh, 0xFF, 0` | - |
| HORIZONTAL  | LMMM | `sxl, sxh, syl, syh, wl, wh, hl, hh, 0xFF, 0` | TEOR |
| HORIZONTAL  | YMMM | `sxl, sxh, syl, syh, wl, wh, hl, hh, 0xFF, 0` | - |
| HORIZONTAL  | HMMV | `sxl, sxh, syl, syh, wl, wh, hl, hh, 0xFF, 0` | - |
| HORIZONTAL  | LMMV | `sxl, sxh, syl, syh, wl, wh, hl, hh, 0xFF, 0` | TOR |



`s` means source  
`d` means destination

`syh` is source page  
`dyh` is destination page

The logical commands are arbitrarily chosen.

## Requirements
* **Run:** MSX2 or higher, MSX-DOS
* **Build:** SDCC v4.2 or higher (tested with v4.6)