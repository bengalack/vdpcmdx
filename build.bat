@set NAME=vdpcmdx

@REM --code-loc value below is found from the dos_crt.sym file - add the value of _HEADER0 to .org (0x100)
sdasz80 -o -s -p -w out\dos_crt.rel src\dos_crt.s
sdasz80 -o -s -p -w out\vdp.rel src\vdp.s
sdasz80 -o -s -p -w out\bioshelper.rel src\bioshelper.s
@REM sdcc --code-loc 0x010E --data-loc 0 -mz80 --no-std-crt0 --opt-code-speed out\dos_crt.rel out\vdp.rel src\main.c -o out\%NAME%.ihx
sdcc --code-loc 0x0180 --data-loc 0 -mz80 --no-std-crt0 --opt-code-speed out\dos_crt.rel out\vdp.rel out\bioshelper.rel src\main.c -o out\%NAME%.ihx

makebin -p -o 0x100 out\%NAME%.ihx dska\%NAME%.com