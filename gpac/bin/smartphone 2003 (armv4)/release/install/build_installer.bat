set OLDDIR=%CD%
cd /d %~dp0

for /f "delims=" %%a in ('svnversion') do set gpac_revision=%%a

ECHO [Version] > gpaccab.inf
ECHO Provider = "GPAC 0.5.0-r%gpac_revision%" >> gpaccab.inf
type gpac.inf >> gpaccab.inf

CabWiz gpaccab.inf

ECHO off

ECHO [CEAppManager]> gpac.ini
ECHO Version = 0.5.0-r%gpac_revision%>> gpac.ini
ECHO Component = GPAC for Windows Mobile>> gpac.ini
ECHO [GPAC for Windows Mobile]>> gpac.ini
ECHO Description = GPAC MPEG-4 Player>> gpac.ini
ECHO Uninstall = GPAC Osmophone>> gpac.ini
ECHO IconFile = ..\..\..\..\doc\osmo4.ico>> gpac.ini
ECHO IconIndex = 0 >> gpac.ini
ECHO CabFiles = gpaccab.cab >> gpac.ini

ECHO on

ezsetup -l english -i gpac.ini -r readme.txt -e ../../../../COPYING -o gpac.exe
rename gpac.exe "GPAC_0.5.0-r%gpac_revision%_WindowsMobile.exe"
DEL gpaccab.cab
DEL gpaccab.inf
DEL gpac.ini
DEL *.tmp

cd /d %OLDDIR%
