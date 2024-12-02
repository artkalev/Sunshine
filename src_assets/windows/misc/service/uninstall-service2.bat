@echo off

rem Stop and delete the legacy SunshineSvc service
net stop sunshinesvc2
sc delete sunshinesvc2

rem Stop and delete the new SunshineService service
net stop SunshineService2
sc delete SunshineService2
