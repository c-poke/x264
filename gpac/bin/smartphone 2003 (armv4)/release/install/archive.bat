set OLDDIR=%CD%
cd /d %~dp0

for /f "delims=" %%a in ('svnversion ') do set gpac_revision=%%a

zip "GPAC_0.5.0-r%gpac_revision%_WindowsMobile.zip" ../*.dll ../*.exe ../*.plg

cd /d %OLDDIR%
