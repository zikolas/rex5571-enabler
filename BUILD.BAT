@echo off
rem Build REXENA.EXE with Open Watcom 1.9 (16-bit, real mode).
rem Expects the Watcom environment to be set, e.g.:
rem   set WATCOM=C:\WATCOM
rem   set INCLUDE=C:\WATCOM\H
rem   set PATH=C:\WATCOM\BINW;%PATH%
wcc -ms REXENA.C -fo=REXENA.obj
if errorlevel 1 goto end
wlink system dos name REXENA.exe file REXENA.obj
:end
