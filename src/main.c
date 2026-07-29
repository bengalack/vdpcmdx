// ---------------------------------------------------------------------------
// Assumptions:
//  * We start in DOS, hence 0x0038 already contains 0xC3 (jp)
//  * There are no active line (or other non-VBLANK-) interrupts enabled
//
// Notes:
//  * There is no support for global initialisation of RAM variables in this config
//  * SORRY! for the Hungarian notation, but is helps me when mixing asm and c
//      * Prefixes:
//      * s  = signed char    (s8)
//      * u  = unsigned char  (u8)
//      * i  = signed short   (s16)
//      * n  = unsigned short (u16)
//      * l  = signed long    (s32)
//      * ul = unsigned long  (u32)
//      * f  = float
//      * p  = pointer
//      * o  = object (struct)
//      * a  = array (single or multi-dim)
//      * b  = bool
//      * sz = zero terminated string (C/SDCC adds the zero automatically)
//      * g_ = global
//      
//      Postfixes:
//      * NI = No Interrupt allowed
//
// author: pal.hansen@gmail.com
// ---------------------------------------------------------------------------

#include <stdio.h>      // herein be sprintf
#include <string.h>     // memcpy/memset/strcmp
#include <stdbool.h>

// Typedefs & defines --------------------------------------------------------
//
#define halt()				{__asm halt __endasm;}
#define enableInterrupt()	{__asm ei __endasm;}
#define disableInterrupt()	{__asm di __endasm;}
#define break()				{__asm in a,(0x2e) __endasm;} // for debugging. may be risky to use as it trashes A
#define arraysize(arr)      (sizeof(arr)/sizeof((arr)[0]))

#define VDPCMD_LMMM		    0b10010000 // LOGICAL COPY BLOCK
#define VDPCMD_LMMV		    0b10000000 // LOGICAL FILL
#define VDPCMD_LMCM         0b10100000 // "LOGICAL" (PIXEL) MOVE VRAM > CPU/RAM

#define VDPCMD_HMMC		    0b11110000 // FAST COPY BLOCK FROM MEM (2 and 2 pix horz)
#define VDPCMD_HMMM		    0b11010000 // FAST COPY BLOCK (2 and 2 pix horz)
#define VDPCMD_YMMM         0b11100000 // FASTEST COPY BLOCK (only Y differs)
#define VDPCMD_HMMV		    0b11000000 // FAST FILL (2 and 2 pix horz)

// #define VDPCMD_LINE		    0b01110000 // LINE

#define LOGICAL_OP_IMP      0b0000 // DC=SC
#define LOGICAL_OP_AND      0b0001 // DC=SCxDC
#define LOGICAL_OP_OR       0b0010 // DC=SC+DC
#define LOGICAL_OP_EOR      0b0011 // DC=SCxDC+SCxDC
#define LOGICAL_OP_NOT      0b0100 // DC=SC

#define LOGICAL_OP_TIMP     0b1000 // if SC=0 then DC=DC else DC=SC
#define LOGICAL_OP_TAND     0b1001 // if SC=0 then DC=DC else DC=SCxDC
#define LOGICAL_OP_TOR      0b1010 // if SC=0 then DC=DC else DC=SC+DC
#define LOGICAL_OP_TEOR     0b1011 // if SC=0 then DC=DC else DC=SCxDC+SCxDC
#define LOGICAL_OP_TNOT     0b1100 // if SC=0 then DC=DC else DC=SC

#define NUM_TESTS           5

#define CPU_READ            0 // 0 for write
#define BLOCK_SIZE          6000 // instructions
#define SCREEN_MODE         8 // 5? 8?

enum raster_location { VBLANK, ACTIVE_AREA, RASTER_LOCATION_COUNT };
enum orientation { LANDSCAPE, PORTRAIT, ORIENTATION_COUNT };
enum condition { NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU, CONDITION_COUNT };
enum freq_variant { NTSC, PAL, FREQ_COUNT };
enum line_variant { NORMAL192, EXTENDED212, LINE_VARIANT_COUNT };

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef signed long         s32;
typedef unsigned long       u32;

#define MAX_SYS_LEN         28

#define PIX_LEN_BIGL        0 // pixels
#define PIX_LEN_BIGH        1 // pixels

#define PIX_LEN_SMALLL      40 // pixels (changing to 30 give same results for landscape but around 20 less pixels for portrait)
#define PIX_LEN_SMALLH      0 // pixels

#define RECT_LANDSCAPE      PIX_LEN_BIGL,PIX_LEN_BIGH,PIX_LEN_SMALLL,PIX_LEN_SMALLH // 4 bytes
#define RECT_PORTRAIT       PIX_LEN_SMALLL,PIX_LEN_SMALLH,PIX_LEN_BIGL,PIX_LEN_BIGH // 4 bytes

#define RECT_LINE           PIX_LEN_BIGL,PIX_LEN_BIGH,PIX_LEN_SMALLL,PIX_LEN_SMALLH // 4 bytes

typedef struct {
    u8                      wl;     // line: longest (l)
    u8                      wh;     // line: longest (h)
    u8                      hl;     // line: shortest (l)
    u8                      hh;     // line: shortest (h)
    u8                      color;  //
    u8                      arg;    // line: isVert (bool)
} VDPParams; // NOTE: Command is not part of this

// Declarations (see .s-file) ------------------------------------------------
//
extern u8       getMSXType(void);
extern u8       getCPU(void);
extern void     changeCPU(u8 uMode);

extern void     enableTurbo(bool bEnable) __preserves_regs(e,h,l,iyl,iyh);
extern bool     isTurboEnabled(void) __preserves_regs(d,e,h,l,iyl,iyh);
extern bool     hasTurboFeature(void) __preserves_regs(d,e,h,l,iyl,iyh);

extern u8       changeMode(u8 uModeNum); 
extern void     setLineWidth(u8 uWidth);
extern void     customISR(void);
extern void     eternalVDPHammeringByCPU(void);
extern void     print(u8* szMessage);
extern u8       waitForKey(void);
extern bool     userOutputsToScreen(void);

extern void     setVDPCmdParamsNI(u8 uPageCode, VDPParams* p); // uPageCod:e SSSSDDDD, S=source page, D=dest page
extern void     executeCmdWithPreppedParamsNI(u8 uCmd);
extern void     waitForVDPCmd(void);
extern u16      countWrittenPixelsNI(u16 nNumPixels); // Assume NI
extern bool     getPALRefreshRate(void);
extern void     setPALRefreshRate(bool bEnabled);
extern void     setVRAMAddress(u8 uBitCodes, u16 nVRAMAddress);
extern void     vdpSpritesEnabled(bool bEnabled);
extern void     vdpScreenEnabled(bool bEnabled);
extern void     vdpSet212Lines(bool b212);
extern void     vdpEnableLineInterruptNI(bool bEnable);
extern void     vdpSetInterruptLine(u8 uLine);

// Consts --------------------------------------------------------------------
//
const u8                g_szVersion[]       = "2.1";
const u8                g_szErrorMSX[]      = "MSX2 or higher is required";
const u8                g_szTopLine[]       = "VDPCMDX v%s. screen 8, %s lines, %dHz%s, %s\r\n";
                                            //"                             " // 29 chars (turbo r)
const u8                g_szFullLine[]      = "-------------------------------------------------------------------------------\r\n";
const u8                g_szHeader1[]       = "                   NORMAL   |   NO SPR   |   NO SCR   | NORMAL+CPU | NO SCR+CPU\r\n";
// const u8                g_szHeader2[]       = " # OPERATION     THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL\r\n";
const u8                g_szHeader2[]       = " # OPERATION     THIS  %s | THIS  %s | THIS  %s | THIS  %s | THIS  %s\r\n";
const u8                g_szLastwords[]     = "1-5:landscape, 6-10:portrait, first 10:raster beam in VBLANK, last 10:ACTIVE ";
const u8                g_szResultLine[]    = "%2d %s    %5hu %5ld |%5hu %5ld |%5hu %5ld |%5hu %5ld |%5hu %5ld\r\n";
const u8                g_szREAL[]          = "REAL";
const u8                g_szDIFF[]          = "DIFF";
                                          //"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]      = "Usage:vdpcmdx.com [opt][sys]\r\n"
                                            "\r\n"
                                            "Counts pixels handled by the\r\n"
                                            "VDP CMD Engine in one frame.\r\n"
                                            "Output is written to stdout.\r\n"
                                            "Unless output is redirected,\r\n"
                                            "program exits in width 80.\r\n"
                                            "\r\n"
                                            "Version: %s\r\n"
                                            "\r\n"
                                            "Options (opt):\r\n"
                                            " -h Show this help message\r\n"
                                            // " -5 Screen 5 (default: 8)\r\n"
                                            " -p PAL (default: current)\r\n"
                                            " -n NTSC (default: current)\r\n"
                                            " -l 212 lines (default: 192)\r\n"
                                            " -d Show data as diff vs REAL\r\n"
                                            "\r\n"
                                            "sys: Show sys name in report\r\n";

const u8* const         aTEST_NAME[NUM_TESTS] = \
                        {
                             "Copy HMMM"
                            ,"Copy LMMM"
                            ,"Copy YMMM"
                            ,"Fill HMMV"
                            ,"Fill LMMV"
                            // ,"Line     "
                        };

const u8 const          aEXECUTE_CMD[NUM_TESTS] = \
                        {
                             VDPCMD_HMMM
                            ,VDPCMD_LMMM | LOGICAL_OP_TEOR  // just random logical op (which does not become 0)
                            ,VDPCMD_YMMM
                            ,VDPCMD_HMMV
                            ,VDPCMD_LMMV | LOGICAL_OP_TOR   // just random logical op  (which does not become 0)
                            // ,VDPCMD_LINE | LOGICAL_OP_EOR   // just random logical op (which does not become 0)
                        };

const u16 const         aTARGETS[LINE_VARIANT_COUNT][RASTER_LOCATION_COUNT][FREQ_COUNT][ORIENTATION_COUNT][NUM_TESTS][CONDITION_COUNT] = 
                        {
                            { // NORMAL192
                                { // VBLANK
                                    { // NTSC
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1039, 1050, 1054, 1032, 1053 }, 
                                            { 729, 740, 740, 724, 739 },
                                            { 1446, 1471, 1474, 1302, 1336 },
                                            { 1940, 1944, 1961, 1864, 1901 },
                                            { 972, 976, 985, 933, 984 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1019, 1029, 1032, 1009, 1032 }, 
                                            { 725, 736, 736, 715, 732 },
                                            { 1414, 1440, 1444, 1275, 1310 },
                                            { 1868, 1871, 1887, 1843, 1856 },
                                            { 957, 961, 971, 944, 941 }
                                        }
                                    },
                                    { // PAL
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1802, 1813, 1815, 1682, 1815 }, 
                                            { 1265, 1275, 1275, 1257, 1274 },
                                            { 2512, 2538, 2541, 2212, 2302 },
                                            { 3360, 3362, 3380, 3238, 3275 },
                                            { 1684, 1688, 1697, 1637, 1638 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1765, 1776, 1778, 1756, 1778 }, 
                                            { 1258, 1268, 1269, 1245, 1261 },
                                            { 2461, 2485, 2488, 2220, 2254 },
                                            { 3229, 3231, 3247, 3203, 3240 },
                                            { 1659, 1662, 1670, 1602, 1630 }
                                        }
                                    }
                                },
                                { // ACTIVE_AREA
                                    { // NTSC
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1916, 2673, 2854, 1156, 2853 }, 
                                            { 1339, 1971, 2003, 771, 2002 },
                                            { 2111, 3799, 3996, 1165, 3618 },
                                            { 4005, 4195, 5313, 1935, 5139 },
                                            { 1908, 2106, 2668, 969, 2604 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1916, 2659, 2796, 1132, 2795 }, 
                                            { 1332, 1955, 1993, 705, 1981 },
                                            { 2093, 3689, 3914, 1153, 3477 },
                                            { 3920, 4120, 5094, 1938, 5031 },
                                            { 1869, 2106, 2626, 969, 2547 }
                                        }
                                    },
                                    { // PAL
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1915, 2673, 2854, 1156, 2854 }, 
                                            { 1339, 1971, 2004, 679, 2002 },
                                            { 2111, 3797, 3996, 1165, 3528 },
                                            { 4005, 4195, 5313, 2123, 5140 },
                                            { 1908, 2106, 2668, 969, 2604 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1916, 2659, 2796, 1132, 2675 }, 
                                            { 1332, 1955, 1993, 755, 1981 },
                                            { 2095, 3689, 3914, 1153, 3544 },
                                            { 3922, 4120, 5094, 2109, 5021 },
                                            { 1869, 2106, 2627, 1135, 2547 }
                                        }
                                    }
                                }
                            },
                            { // EXTENDED212
                                { // VBLANK
                                    { // NTSC
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 741, 752, 755, 732, 755 }, 
                                            { 518, 530, 530, 514, 530 },
                                            { 1027, 1051, 1055, 923, 959 },
                                            { 1384, 1387, 1403, 1320, 1358 },
                                            { 694, 696, 706, 639, 659 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 726, 738, 740, 717, 739 }, 
                                            { 517, 527, 526, 509, 523 },
                                            { 1005, 1030, 1036, 903, 939 },
                                            { 1335, 1340, 1356, 1311, 1348 },
                                            { 682, 687, 696, 670, 683 }
                                        }
                                    },
                                    { // PAL
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1502, 1514, 1517, 1402, 1517 }, 
                                            { 1053, 1065, 1064, 1047, 1064 },
                                            { 2094, 2120, 2122, 1887, 1923 },
                                            { 2804, 2807, 2822, 2699, 2730 },
                                            { 1405, 1408, 1418, 1394, 1416 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 1472, 1484, 1486, 1463, 1485 }, 
                                            { 1049, 1060, 1061, 1031, 1053 },
                                            { 2049, 2077, 2080, 1849, 1883 },
                                            { 2697, 2700, 2716, 2671, 2708 },
                                            { 1383, 1389, 1396, 1343, 1321 }
                                        }
                                    }
                                },
                                { // ACTIVE_AREA
                                    { // NTSC
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 2116, 2952, 3153, 1173, 3152 }, 
                                            { 1479, 2177, 2213, 850, 2211 },
                                            { 2329, 4193, 4413, 1284, 3996 },
                                            { 4422, 4631, 5869, 2342, 5672 },
                                            { 2106, 2325, 2946, 1275, 2942 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 2116, 2937, 3087, 1254, 3087 }, 
                                            { 1470, 2159, 2201, 745, 2188 },
                                            { 2312, 4074, 4322, 1284, 3915 },
                                            { 4331, 4548, 5626, 2138, 5619 },
                                            { 2063, 2324, 2901, 1069, 2881 }
                                        }
                                    },
                                    { // PAL
                                        { // LANDSCAPE
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 2115, 2952, 3152, 1275, 2955 }, 
                                            { 1479, 2176, 2213, 749, 2211 },
                                            { 2330, 4194, 4412, 1284, 3996 },
                                            { 4422, 4632, 5868, 2135, 5678 },
                                            { 2106, 2325, 2946, 1275, 2877 }
                                        },
                                        { // PORTRAIT
                                            // TEST # (NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU)
                                            { 2115, 2937, 3088, 1249, 3087 }, 
                                            { 1470, 2160, 2201, 745, 2178 },
                                            { 2313, 4074, 4321, 1271, 3914 },
                                            { 4330, 4548, 5627, 2328, 5619 },
                                            { 2063, 2324, 2901, 1069, 2881 }
                                        }
                                    }
                                }
                            }
                        };

const VDPParams const   CLEAR_FULL_AREA = {PIX_LEN_BIGL, PIX_LEN_BIGH, PIX_LEN_BIGL, PIX_LEN_BIGH,    0, 0};
const VDPParams const   FILL_FULL_AREA  = {PIX_LEN_BIGL, PIX_LEN_BIGH, PIX_LEN_BIGL, PIX_LEN_BIGH, 0xFF, 0};

const VDPParams const   aCLEAR_PARAMS[ORIENTATION_COUNT] = \
                        {
                            {RECT_LANDSCAPE, 0x00, 0},
                            {RECT_PORTRAIT,  0x00, 0}
                        };


const VDPParams const   aTEST_PARAMS[ORIENTATION_COUNT][NUM_TESTS] = \
                        {
                            {   // LANDSCAPE
                                 {RECT_LANDSCAPE, 0xFF, 0}  // Copy HMMM
                                ,{RECT_LANDSCAPE, 0xFF, 0}  // Copy LMMM
                                ,{RECT_LANDSCAPE, 0xFF, 0}  // Copy YMMM
                                ,{RECT_LANDSCAPE, 0xFF, 0}  // Fill HMMV
                                ,{RECT_LANDSCAPE, 0xFF, 0}  // Fill LMMV
                                // ,{RECT_LINE,      0xFF, 0}  // Line
                            },
                            {   // PORTRAIT
                                 {RECT_PORTRAIT, 0xFF, 0}   // Copy HMMM
                                ,{RECT_PORTRAIT, 0xFF, 0}   // Copy LMMM
                                ,{RECT_PORTRAIT, 0xFF, 0}   // Copy YMMM
                                ,{RECT_PORTRAIT, 0xFF, 0}   // Fill HMMV
                                ,{RECT_PORTRAIT, 0xFF, 0}   // Fill LMMV
                                // ,{RECT_LINE,     0xFF, 1}   // Line
                            }
                        };

// RAM variables -------------------------------------------------------------
//
u8                      g_auBuffer[ 256 ];      // temp/general buffer here to avoid stack explosion
void* __at(0x0039)      g_pInterrupt;           // We assume that 0x0038 already holds 0xC3 (JP) in dos mode at startup
void*                   g_pInterruptOrg;
u8                      g_uFreqVariantOrg;      // NTSC or PAL
u8                      g_uFreqVariant;         // NTSC or PAL
volatile bool           g_bRecordingEnabled;    // Gate 1
volatile bool           g_bRecordingInitiated;  // Gate 2
volatile bool           g_bRecordingCPUHammering;

u8                      g_auSysStr[MAX_SYS_LEN];// name of system

u8                      g_uXPosL;               // We need this value to right hand side of screen in YMMM

u16                     g_anResult[RASTER_LOCATION_COUNT][ORIENTATION_COUNT][CONDITION_COUNT][NUM_TESTS];
u8*                     pHAMMERING_CODE_BLOCK; // set to _HEAP_start in initVarsAndRig; avoiding a static array which bloats the COM binary
extern u8               HEAP_start[];         // asm symbol _HEAP_start: first byte after all program data

// ---------------------------------------------------------------------------
void fillPageBg(u8 uPage, VDPParams* p)
{
    disableInterrupt();

    setVDPCmdParamsNI(uPage, p);
    executeCmdWithPreppedParamsNI(VDPCMD_HMMV);
    enableInterrupt();

    waitForVDPCmd(); // this one sets DI + EI
}

// ---------------------------------------------------------------------------
void setCustomISR(void)
{
    g_bRecordingEnabled = false;
    g_bRecordingInitiated = false;
    g_bRecordingCPUHammering = false;

    disableInterrupt();
    g_pInterruptOrg = g_pInterrupt;
    g_pInterrupt    = &customISR;
    enableInterrupt();
}

// ---------------------------------------------------------------------------
void restoreOriginalISR(void)
{
    disableInterrupt();
    g_pInterrupt = g_pInterruptOrg;
    enableInterrupt();
}

// ---------------------------------------------------------------------------
// Cannot have box at (0,0) to make sense of splitting orientation using YMMM
void alignBox(u8 uOrientation)
{
    g_uXPosL = (u8)((u16)256 - aTEST_PARAMS[uOrientation][0].wl);
}

// ---------------------------------------------------------------------------
// Puts hammering code in the heap (without allocating it - too big for standard
// SDCC allocation which is 1kB)
void initVarsAndRig(void)
{
    strcpy(g_auSysStr,"<system/model name not set>");

    pHAMMERING_CODE_BLOCK = HEAP_start;
    g_uFreqVariantOrg = getPALRefreshRate()? PAL : NTSC;
    g_uFreqVariant = g_uFreqVariantOrg;
    memset(g_anResult, 0, sizeof(g_anResult));
    alignBox(0); // sets g_uXPosL

    // We need unrolled VDP hammering code where the CPU can run for a full frame
    // i.e. > 70000 cycles. We'll use INI/OUTI, which is 18 MSX cycles. Or IN/OUT (12)
    // 70 000 / 18 ~ 4000 instructions. INI is two bytes, so 8000 bytes
    // 70 000 / 12 ~ 6000 instructions. INI is two bytes, so 12000 bytes
    // See BLOCK_SIZE
    // We'll put this at the end of this small file. There should be more than
    // enough space
    // INI:       ED A2
    // OUTI:      ED A3
    // IN A,(n):  DB n
    // OUT (n),a: D3 n
    u8* p = pHAMMERING_CODE_BLOCK;
    u16 p_org = (u16)p;

    for(u16 n=0; n < BLOCK_SIZE; n++)
    {
        // *p++ = 0x00; // NOP (for debugging)
        // *p++ = 0x00;

#if CPU_READ == 1
        // *p++ = 0xED;
        // *p++ = 0xA2;
        *p++ = 0xDB; // IN A,(n)
        *p++ = 0x98; // VDPIO
#else
        // *p++ = 0xED;
        // *p++ = 0xA3;
        *p++ = 0xD3; // OUT (n),A
        *p++ = 0x98; // VDPIO
#endif
    } 

    // Add a tiny safety at the end (loop back), but this code should never be reached.
    *p++ = 0xC3; // JP to the start of the INI loop
    *p++ = (u8) (p_org       & 0xFF); // low byte of address
    *p++ = (u8)((p_org >> 8) & 0xFF); // high byte of address
}

// ---------------------------------------------------------------------------
u16 countPixels(u8 uOrientation, u8 uTest)
{
    disableInterrupt();
    setVDPCmdParamsNI(1<<4, &aTEST_PARAMS[uOrientation][uTest]); // Read needs SRC coords (<<)
    executeCmdWithPreppedParamsNI(VDPCMD_LMCM);

    const VDPParams* p = &aTEST_PARAMS[uOrientation][uTest];

    u16 nW = ((p->wh) << 8) + p->wl;
    u16 nH = ((p->hh) << 8) + p->hl;
    u16 nNumPixels = nW * nH;

    u16 nCount = countWrittenPixelsNI(nNumPixels);
    enableInterrupt();

    return nCount;
}

// ---------------------------------------------------------------------------
u16 runTestSingle(u8 uOrientation, u8 nTest, bool bUseCPU, bool bVBlank)
{
    halt(); // SYNC 1

        alignBox(uOrientation);

        // PREPARE PARAMS
        disableInterrupt();
        setVDPCmdParamsNI(1, &aTEST_PARAMS[uOrientation][nTest]);
        g_bRecordingEnabled = true;
        // g_bVBlankArea = bVBlank;
        
        if(bUseCPU)
        {
#if CPU_READ == 1
            setVRAMAddress(0, 0); // read or write at page 0, if write, we write 0xFF, same as current color
#else
            setVRAMAddress(0b01000000, 0); // sets write flag
#endif
        }

        vdpEnableLineInterruptNI(true);
        enableInterrupt();

    if(bVBlank)
    {
        halt(); // get past the line interrupt at line 0
        halt(); // SYNC 2
    }
    else
    {
        halt(); // get past the line interrupt at line 0
        halt(); // SYNC 2
        halt(); // Skip to the line interrupt at line 0
    }

    executeCmdWithPreppedParamsNI(aEXECUTE_CMD[nTest]); // right after int. ignoring DI. must be kicked off ASAP after int.
    g_bRecordingInitiated = true; // at next interrupt, we will abort the CMD
    g_bRecordingCPUHammering = bUseCPU;

    // Wait until interrupt kicks in and we can continue
    if(bUseCPU)
        eternalVDPHammeringByCPU();
    else
        while(g_bRecordingEnabled){}

    disableInterrupt();
    vdpEnableLineInterruptNI(false); // crucial to remove this function
    enableInterrupt();

    u16 nCount = countPixels(uOrientation, nTest);

    fillPageBg(1, &aCLEAR_PARAMS[uOrientation]); // clean up for next before next test

    return nCount;
}

// ---------------------------------------------------------------------------
void runTests(u8 uCondition)
{
    for(u8 uOrientation = 0; uOrientation < ORIENTATION_COUNT; uOrientation++)
        for(u8 n = 0; n < NUM_TESTS; n++)
            for(u8 v = 0; v < RASTER_LOCATION_COUNT; v++)
                g_anResult[v][uOrientation][n][uCondition] = runTestSingle(uOrientation, n, uCondition >= NORMAL_CPU, v == VBLANK);
}

// ---------------------------------------------------------------------------
void printReport(bool bDiffMode, bool bPALSet, bool bNTSCSet, bool b212Set)
{
    sprintf(g_auBuffer,
            g_szTopLine,
            g_szVersion,
            b212Set? "212" : "192",
            g_uFreqVariant==PAL ? 50 : 60,
            (!bPALSet && !bNTSCSet) ? " (detected)" : "",
            g_auSysStr
           );

    print(g_auBuffer);

    print(g_szHeader1);

    const u8* p = bDiffMode? g_szDIFF : g_szREAL;
    sprintf(g_auBuffer, g_szHeader2, p, p, p, p, p);
    print(g_auBuffer);

    print(g_szFullLine);


    s32 lShowNumber[CONDITION_COUNT];

    u8 uLineVariant = b212Set? EXTENDED212 : NORMAL192;

    for(u8 r = 0; r<RASTER_LOCATION_COUNT; r++)
    {
        u8 uLine = 1;

        for(u8 o = 0; o < ORIENTATION_COUNT; o++)
        {
            for(u8 t = 0; t < NUM_TESTS; t++)
            {
                for(u8 c = 0; c < CONDITION_COUNT; c++) // set the correct number
                {
                    if(bDiffMode)
                        lShowNumber[c] = (s32)(g_anResult[r][o][t][c]) - (s32)(aTARGETS[uLineVariant][r][g_uFreqVariant][o][t][c]);
                    else
                        lShowNumber[c] = (s32)(aTARGETS[uLineVariant][r][g_uFreqVariant][o][t][c]);
                }

                sprintf(g_auBuffer,
                        g_szResultLine,
                        uLine,
                        aTEST_NAME[t],
                        g_anResult[r][o][t][NORMAL],
                        lShowNumber[NORMAL],
                        g_anResult[r][o][t][NO_SPRITES],
                        lShowNumber[NO_SPRITES],
                        g_anResult[r][o][t][NO_SCREEN],
                        lShowNumber[NO_SCREEN],
                        g_anResult[r][o][t][NORMAL_CPU],
                        lShowNumber[NORMAL_CPU],
                        g_anResult[r][o][t][NO_SCREEN_CPU],
                        lShowNumber[NO_SCREEN_CPU]
                    );

                print(g_auBuffer);
                uLine++;
            }
        }
    }

    print(g_szFullLine);
    print(g_szLastwords);
}

// ---------------------------------------------------------------------------
// A little hacky, but we want 26,5 lines of text on the screen for a brief
// moment
void clearScreenRaw(void)
{
    // setVRAMAddress(0b01000000, 0x01800); // sets write flag and point to NAME TABLE
    setVRAMAddress(0b01000000, 0x00000); // sets write flag and point to NAME TABLE

__asm
    di
    ld de,#80*26    ; 80 columns, 26 lines
    ld b,#0x20      ; space
    ld c,#0x98      ; vdp io port
00001$:
    out (c),b
    dec de
    ld a, d
    or e
    jr nz,00001$

    ld a,#0x2d      ; dash
    ld b,#79        ; one line
00002$:
    out (0x98),a
    djnz 00002$

    ld a,#0x20      ; space
    out (0x98),a

    ei
__endasm;
}

// ---------------------------------------------------------------------------
void printHelp(void)
{
    sprintf(g_auBuffer, g_szHelptext, g_szVersion);
    print(g_auBuffer);
}

// ---------------------------------------------------------------------------
// Do the test. If interactive/screen mode, enable 26,5 lines for output +
// wait for keypress before returning. When output is redirected to file,
// we don't do this.
u8 main(char** argv, u8 argc)
{
    bool bNTSCSet = false;
    bool bPALSet = false;
    bool b212Set = false;
    bool bDiffMode = false;

    initVarsAndRig();

    // ------------------------------------
    // Check params
    if(argc > 0)
    {
        for(u8 n=0; n < argc; n++)
        {
            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
                printHelp();
                return 0;
            }
            else if(strcmp(argv[n], "-p") == 0)
            {
                bPALSet = true;
                g_uFreqVariant = PAL;
            }
            else if(strcmp(argv[n], "-n") == 0)
            {
                bNTSCSet = true;
                g_uFreqVariant = NTSC;
            }
            else if(strcmp(argv[n], "-l") == 0)
                b212Set = true;
            else if(strcmp(argv[n], "-d") == 0)
                bDiffMode = true;
            else
            {
                if(strlen(argv[n]) > MAX_SYS_LEN-1) // minus the zero terminator
                {
                    sprintf(g_auBuffer, "ERROR:Sys name too long (>%d)", MAX_SYS_LEN-1);
                    print(g_auBuffer);
                    return 1;
                }

                strcpy(g_auSysStr, argv[n]);
            }
        }
    }

    // ------------------------------------
    // Initialize
    u8 uType = getMSXType();
    s8 sOrgCPU = -1;
    bool bRestoreTurbo = false;

    if(uType == 0)
    {
        print(g_szErrorMSX);
        return 1;
    }
    else if(uType == 3) // MSX turbo R
    {
        u8 uCPU = getCPU();
        if(uCPU != 0)
        {
            sOrgCPU = (s8)uCPU;
            changeCPU(0); // 0=Z80 (ROM) mode, 1=R800 ROM  mode, 2=R800 DRAM mode
        }
    }

    if(hasTurboFeature())
    {
        if(isTurboEnabled())
        {
            bRestoreTurbo = true;
            enableTurbo(false);
        }
    }

    bool hasScreenOutput = userOutputsToScreen();

    setCustomISR(); 

    u8 uPrevScrMode = changeMode(SCREEN_MODE);

    if(bPALSet)
        setPALRefreshRate(true);

    if(bNTSCSet)
        setPALRefreshRate(false);

    vdpSet212Lines(b212Set);
    vdpSetInterruptLine(0);

    fillPageBg(0, &FILL_FULL_AREA);
    fillPageBg(1, &CLEAR_FULL_AREA);

    // ---------------------------------------------
    // READY! Set conditions for tests and run tests
    vdpSpritesEnabled(true);
    vdpScreenEnabled(true);
    runTests(NORMAL);

    vdpSpritesEnabled(false);
    runTests(NO_SPRITES);
    vdpSpritesEnabled(true);

    vdpScreenEnabled(false);
    runTests(NO_SCREEN);
    vdpScreenEnabled(true); 

    runTests(NORMAL_CPU);

    vdpScreenEnabled(false);
    runTests(NO_SCREEN_CPU);
    vdpScreenEnabled(true);

    // ----------------------------------
    // Start cleanup before returning to DOS
    restoreOriginalISR();

    if(hasScreenOutput) // prepare wide and large screen when not redirecting to file
    {
        setLineWidth(80);
        changeMode(0);

        vdpSet212Lines(true);
        clearScreenRaw();
    }
    else
    {
        changeMode(uPrevScrMode);
    }

    // ----------------------------------
    // Show summary
    printReport(bDiffMode, bPALSet, bNTSCSet, b212Set);

    if(sOrgCPU != -1)
        changeCPU(sOrgCPU);

    if(bRestoreTurbo)
        enableTurbo(true);

    if(hasScreenOutput)
    {
        waitForKey();
        vdpSet212Lines(false);
    }
    else if(b212Set)
        vdpSet212Lines(false);

    if(bPALSet || bNTSCSet)
        setPALRefreshRate(g_uFreqVariantOrg == PAL);

    return 0;
}