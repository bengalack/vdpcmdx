@echo Make sure MSXDOS is present in the dska-folder
@REM C:\tools\openmsx21x\openmsx.exe -machine Sanyo_phc-70FD2 -diska dska/ -command "debug set_watchpoint read_io 0x2E" -command "set throttle off" -command "after time 7 {set throttle on}"
@REM C:\tools\openmsx21x\openmsx.exe -machine Philips_NMS_8255 -diska dska/ -command "debug set_watchpoint read_io 0x2E" -command "set throttle off" -command "after time 7 {set throttle on}"
C:\tools\openmsx21x\openmsx.exe -machine Panasonic_FS-A1GT -diska dska/ -command "debug set_watchpoint read_io 0x2E" -command "set throttle off" -command "after time 7 {set throttle on}"
