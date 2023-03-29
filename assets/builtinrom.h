#pragma once
// Built-in rom for PicoSystem_InfoNes Nes emulator
// Is currently Blade Buster (http://hlc6502.web.fc2.com/Bbuster.htm)
// Generated from BladeBuster.nes using
// xxd -i BladBuster.nes
#define BUILTINROMNAME "Blade Buster"
#ifdef __cplusplus
extern "C"
{
#endif
extern const char builtinrom[];
extern unsigned int builtinrom_len;
#ifdef __cplusplus
}
#endif
