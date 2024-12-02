@echo off

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

set SERVICE_NAME=SunshineService
set SERVICE_BIN="%ROOT_DIR%\tools\sunshinesvc2.exe"

rem Set service to demand start. It will be changed to auto later if the user selected that option.
set SERVICE_START_TYPE=demand

rem Remove the legacy SunshineSvc service
net stop sunshinesvc2
sc delete sunshinesvc2

rem Check if SunshineService already exists
sc qc %SERVICE_NAME% > nul 2>&1
if %ERRORLEVEL%==0 (
    rem Stop the existing service if running
    net stop %SERVICE_NAME%

    rem Reconfigure the existing service
    set SC_CMD=config
) else (
    rem Create a new service
    set SC_CMD=create
)

rem Run the sc command to create/reconfigure the service
sc %SC_CMD% %SERVICE_NAME% binPath= %SERVICE_BIN% start= %SERVICE_START_TYPE% DisplayName= "Sunshine Service 2"

rem Set the description of the service
sc description %SERVICE_NAME% "FrostFilms Hacked Sunshine is a self-hosted game stream host for Moonlight, modified to create multi monitor stream"

rem Start the new service
net start %SERVICE_NAME%
