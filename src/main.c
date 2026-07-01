// ---------------------------------------------------------------------------
// Assumptions:
//  * We start in DOS, hence 0x0038 already contains 0xC3 (jp)
//  * There are no line (or other non-VBLANK-) interrupts enabled
//
// Notes:
//  * There is no support for global initialisation of RAM variables in this config
//  * SORRY! for the Hungarian notation, but is helps me when mixing asm and c
//      * Prefixes:
//      * s  = signed char    (s8)
//      * u  = unsigned char  (u8)
//      * i  = signed short   (s16)
//      * n  = unsigned short (u16)
//      * l  = unsigned long  (u32)
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
#include <string.h>     // memcpy/memset
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

enum orientation { LANDSCAPE, PORTRAIT, ORIENTATION_COUNT };
enum condition { NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU, CONDITION_COUNT };
enum freq_variant { NTSC, PAL, FREQ_COUNT };

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef unsigned long       u32;

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

// -------------------------------------------------------------------------
typedef union {
	struct {
		u8  xl,xh,yl,yh;
	};
	u32 xxyy;
} Coords;

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

extern void     setVDPCmdParamsNI(u8 uPageCode, VDPParams* p); // uPageCod:e SSSSDDDD, S=source page, D=dest page
extern void     executeCmdWithPreppedParamsNI(u8 uCmd);
extern void     waitForVDPCmd(void);
extern u16      countWrittenPixelsNI(u16 nNumPixels); // Assume NI

extern void     customISR(void);
extern void     eternalVDPHammeringByCPU(void);

extern void     print(u8* szMessage);
extern bool     getPALRefreshRate(void);
extern void     setVRAMAddress(u8 uBitCodes, u16 nVRAMAddress);
extern void     vdpSpritesEnabled(bool bEnabled);
extern void     vdpScreenEnabled(bool bEnabled);

// Consts --------------------------------------------------------------------
//
const u8                g_szErrorMSX[]      = "MSX2 or higher is required";
const u8                g_szGreeting[]      = "VDP CMDX Timings v0.5. Area pixel amount handled in one frame (%d Hz detected)\r\n";
const u8                g_szFullLine[]      = "--------------------------------------------------------------------------------";
const u8                g_szHeader1[]       = "                NORMAL   |   NO SPR   |   NO SCR   | NORMAL+CPU | NO SCR+CPU\r\n";
const u8                g_szHeader2[]       = " # OPERATION  THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL\r\n";
// const u8                g_szFooter1[]       = "First half of tests is landscape shapes. Second half is portrait shapes.\r\n";
const u8                g_szFooter1[]       = "First half of tests is landscape shapes. Second half is portrait shapes.";
#if CPU_READ == 1
const u8                g_szFooter2[]       = "Testing does not differenciate between VBLANK or not. Hammering is READING.";
#else
const u8                g_szFooter2[]       = "Testing does not differenciate between VBLANK or not. Hammering is WRITING.";
#endif
const u8                g_szResultLine[]    = "%2d %s %5hu %5hu |%5hu %5hu |%5hu %5hu |%5hu %5hu |%5hu %5hu";
const u8                g_szNewLine[]    = "\r\n";

// const u8                g_szReportValues[]  = "% 9s %2s % 5hu.%02d % 5hu % 5hu % 2d.%02d %+ 2d | %2s % 5hu.%02d % 5hu % 5hu % 2d.%02d %+ 2d\r\n";

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

const u16 const         aTARGETS[FREQ_COUNT][ORIENTATION_COUNT][NUM_TESTS][CONDITION_COUNT] = 
                        {
                            { // NTSC
                                { // LANDSCAPE
                                    // TEST #
                                    { 2859, 3708, 3911, 1943, 3858 }, // NORMAL
                                    { 2001, 2709, 2746, 1327, 2744 }, // NO_SPRITES
                                    { 3360, 5252, 5475, 2252, 5019 }, // NO_SCREEN
                                    { 5811, 6024, 7281, 3656, 7018 }, // NORMAL_CPU
                                    { 2803, 3024, 3655, 1843, 3460 }  // NO_SCREEN_CPU
                                },
                                { // PORTRAIT
                                    // TEST #
                                    { 2844, 3678, 3831, 1952, 3809 }, // NORMAL
                                    { 1990, 2689, 2732, 1316, 2716 }, // NO_SPRITES
                                    { 3322, 5111, 5362, 2226, 4948 }, // NO_SCREEN
                                    { 5664, 5886, 6980, 3596, 6879 }, // NORMAL_CPU
                                    { 2749, 3014, 3600, 1803, 3449 }  // NO_SCREEN_CPU
                                }
                            },
                            { // PAL
                                { // LANDSCAPE
                                    // TEST #
                                    { 3621, 4471, 4673, 2751, 4636 }, // NORMAL
                                    { 2536, 3244, 3282, 1843, 3279 }, // NO_SPRITES
                                    { 4425, 6319, 6542, 3248, 6004 }, // NO_SCREEN
                                    { 7230, 7442, 8701, 5068, 8385 }, // NORMAL_CPU
                                    { 3515, 3736, 4367, 2545, 4176 }  // NO_SCREEN_CPU
                                },
                                { // PORTRAIT
                                    // TEST #
                                    { 3590, 4424, 4577, 2648, 4547 }, // NORMAL
                                    { 2522, 3221, 3264, 1848, 3244 }, // NO_SPRITES
                                    { 4367, 6159, 6407, 3200, 5889 }, // NO_SCREEN
                                    { 7024, 7246, 8340, 4949, 8217 }, // NORMAL_CPU
                                    { 3451, 3716, 4302, 2513, 4124 }  // NO_SCREEN_CPU
                                }
                            }
                        };

// VALUES FOR 256x40
// const u16 const         aTARGETS[FREQ_COUNT][ORIENTATION_COUNT][NUM_TESTS][CONDITION_COUNT] = 
//                         {
//                             { // NTSC
//                                 { // LANDSCAPE
//                                     // TEST #
//                                     { 2859, 3708, 3911, 1943, 3858 }, // NORMAL
//                                     { 2001, 2709, 2746, 1327, 2744 }, // NO_SPRITES
//                                     { 3360, 5252, 5475, 2252, 5019 }, // NO_SCREEN
//                                     { 5811, 6024, 7281, 3656, 7018 }, // NORMAL_CPU
//                                     { 2803, 3024, 3655, 1843, 3460 }  // NO_SCREEN_CPU
//                                 },
//                                 { // PORTRAIT
//                                     // TEST #
//                                     { 2844, 3678, 3831, 1952, 3809 }, // NORMAL
//                                     { 1990, 2689, 2732, 1316, 2716 }, // NO_SPRITES
//                                     { 3322, 5111, 5362, 2226, 4948 }, // NO_SCREEN
//                                     { 5664, 5886, 6980, 3596, 6879 }, // NORMAL_CPU
//                                     { 2749, 3014, 3600, 1803, 3449 }  // NO_SCREEN_CPU
//                                 }
//                             },
//                             { // PAL
//                                 { // LANDSCAPE
//                                     // TEST #
//                                     { 3621, 4471, 4673, 2751, 4636 }, // NORMAL
//                                     { 2536, 3244, 3282, 1843, 3279 }, // NO_SPRITES
//                                     { 4425, 6319, 6542, 3248, 6004 }, // NO_SCREEN
//                                     { 7230, 7442, 8701, 5068, 8385 }, // NORMAL_CPU
//                                     { 3515, 3736, 4367, 2545, 4176 }  // NO_SCREEN_CPU
//                                 },
//                                 { // PORTRAIT
//                                     // TEST #
//                                     { 3590, 4424, 4577, 2648, 4547 }, // NORMAL
//                                     { 2522, 3221, 3264, 1848, 3244 }, // NO_SPRITES
//                                     { 4367, 6159, 6407, 3200, 5889 }, // NO_SCREEN
//                                     { 7024, 7246, 8340, 4949, 8217 }, // NORMAL_CPU
//                                     { 3451, 3716, 4302, 2513, 4124 }  // NO_SCREEN_CPU
//                                 }
//                             }
//                         };

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
u8                      g_uFreqVariant;         // NTSC or PAL
volatile bool           g_bRecordingEnabled;    // Gate 1
volatile bool           g_bRecordingInitiated;  // Gate 2
volatile bool           g_bRecordingCPUHammering;

u8                      g_uXPosL;               // We need this value to right hand side of screen in YMMM

u16                     g_anResult[ORIENTATION_COUNT][CONDITION_COUNT][NUM_TESTS];
u8                      HAMMERING_CODE_BLOCK[BLOCK_SIZE*2+3]; // 2 bytes each. This is for the CPU hammering code, which needs to be in RAM
// u8                      NON_ZERO_BLOCK[BLOCK_SIZE];


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
void initVarsAndRig(void)
{
    g_uFreqVariant = getPALRefreshRate()? PAL : NTSC;
    memset(g_anResult, 0, sizeof(g_anResult));
    // memset(NON_ZERO_BLOCK, 0xFF, sizeof(NON_ZERO_BLOCK)); // We put known values here, so any OUTI writes non-zero values
    alignBox(0);

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
    u8* p = HAMMERING_CODE_BLOCK;
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
u16 runTestSingle(u8 uOrientation, u8 nTest, bool bUseCPU)
{
    halt(); // SYNC 1
        alignBox(uOrientation);

        // PREPARE PARAMS
        disableInterrupt();
        setVDPCmdParamsNI(1, &aTEST_PARAMS[uOrientation][nTest]);
        g_bRecordingEnabled = true;
        
        if(bUseCPU)
        {
#if CPU_READ == 1
            setVRAMAddress(0, 0); // read or write at page 0, if write, we write 0xFF, same as current color
#else
            setVRAMAddress(0b01000000, 0); // sets write flag
#endif
        }

        enableInterrupt();

    halt(); // SYNC 2
        executeCmdWithPreppedParamsNI(aEXECUTE_CMD[nTest]); // right after int. ignoring DI. must be kicked off ASAP after int.
        g_bRecordingInitiated = true; // at next interrupt, we will abort the CMD
        g_bRecordingCPUHammering = bUseCPU;

    // Wait until interrupt kicks in and we can continue
    if(bUseCPU)
        eternalVDPHammeringByCPU();
    else
        while(g_bRecordingEnabled){}

    u16 nCount = countPixels(uOrientation, nTest);

    fillPageBg(1, &aCLEAR_PARAMS[uOrientation]); // clean up for next before next test

    return nCount;
}

// ---------------------------------------------------------------------------
void runTests(u8 uCondition)
{
    for(u8 uOrientation = 0; uOrientation < ORIENTATION_COUNT; uOrientation++)
        for(u8 n = 0; n < NUM_TESTS; n++)
            g_anResult[uOrientation][n][uCondition] = runTestSingle(uOrientation, n, uCondition >= NORMAL_CPU);
}

// ---------------------------------------------------------------------------
void printReport(void)
{
    sprintf(g_auBuffer, g_szGreeting, g_uFreqVariant==PAL ? 50 : 60);
    print(g_auBuffer);

    // print(g_szFullLine);
    print(g_szHeader1);
    print(g_szHeader2);
    // print(g_szFullLine);

    u8 uLine;

    uLine = 1;
    for(u8 o = 0; o < ORIENTATION_COUNT; o++)
    {
        for(u8 t = 0; t < NUM_TESTS; t++)
        {
            sprintf(g_auBuffer,
                    g_szResultLine,
                    uLine,
                    aTEST_NAME[t],
                    g_anResult[o][t][NORMAL],
                    aTARGETS[g_uFreqVariant][o][t][NORMAL],
                    g_anResult[o][t][NO_SPRITES],
                    aTARGETS[g_uFreqVariant][o][t][NO_SPRITES],
                    g_anResult[o][t][NO_SCREEN],
                    aTARGETS[g_uFreqVariant][o][t][NO_SCREEN],
                    g_anResult[o][t][NORMAL_CPU],
                    aTARGETS[g_uFreqVariant][o][t][NORMAL_CPU],
                    g_anResult[o][t][NO_SCREEN_CPU],
                    aTARGETS[g_uFreqVariant][o][t][NO_SCREEN_CPU]
                    );

            print(g_auBuffer);
            print(g_szNewLine);
            uLine++;
        }
    }

    uLine = 1;
    for(u8 o = 0; o < ORIENTATION_COUNT; o++)
    {
        for(u8 t = 0; t < NUM_TESTS; t++)
        {
            sprintf(g_auBuffer,
                    g_szResultLine,
                    uLine,
                    aTEST_NAME[t],
                    g_anResult[o][t][NORMAL],
                    aTARGETS[g_uFreqVariant][o][t][NORMAL],
                    g_anResult[o][t][NO_SPRITES],
                    aTARGETS[g_uFreqVariant][o][t][NO_SPRITES],
                    g_anResult[o][t][NO_SCREEN],
                    aTARGETS[g_uFreqVariant][o][t][NO_SCREEN],
                    g_anResult[o][t][NORMAL_CPU],
                    aTARGETS[g_uFreqVariant][o][t][NORMAL_CPU],
                    g_anResult[o][t][NO_SCREEN_CPU],
                    aTARGETS[g_uFreqVariant][o][t][NO_SCREEN_CPU]
                    );

            print(g_auBuffer);
            if(uLine < (NUM_TESTS*2))
                print(g_szNewLine);
            uLine++;
        }
    }

    // print(g_szFullLine);
    // print(g_szFooter1);
    // print(g_szFooter2);
}

// ---------------------------------------------------------------------------
u8 main(void)
{
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

    initVarsAndRig();
    setCustomISR(); 
    
    changeMode(SCREEN_MODE);

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
    // Start cleanup for returning to DOS
    restoreOriginalISR();

    setLineWidth(80);
    changeMode(0);

    // ----------------------------------
    // Show summary
    printReport();

    if(sOrgCPU != -1)
        changeCPU(sOrgCPU);

    if(bRestoreTurbo)
        enableTurbo(true);

    return 0;
}