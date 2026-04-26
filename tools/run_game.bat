@echo off

set SUPERTUXKART_DATADIR=%~dp0

if %PROCESSOR_ARCHITECTURE%==x86 (
Pushd %~dp0\bin\i686\
MinkowskiKart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==AMD64 (
Pushd %~dp0\bin\x86_64\
MinkowskiKart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==ARM64 (
Pushd %~dp0\bin\aarch64\
MinkowskiKart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==ARM (
Pushd %~dp0\bin\armv7\
MinkowskiKart.exe
popd
)
