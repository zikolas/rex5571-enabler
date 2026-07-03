@echo off
rem Build ES1688GO.EXE with Open Watcom 1.9 (16-bit, real mode).
rem Expects the Watcom environment to be set, e.g.:
rem   set WATCOM=C:\WATCOM
rem   set INCLUDE=C:\WATCOM\H
rem   set PATH=C:\WATCOM\BINW;%PATH%
wcc -ms ES1688GO.C -fo=ES1688GO.obj
if errorlevel 1 goto end
wlink system dos name ES1688GO.exe file ES1688GO.obj
:end
