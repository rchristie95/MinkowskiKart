@echo off

if %PROCESSOR_ARCHITECTURE%==x86 (
Pushd %~dp0\bin\i686\
supertuxkart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==AMD64 (
Pushd %~dp0\bin\x86_64\
supertuxkart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==ARM64 (
Pushd %~dp0\bin\aarch64\
supertuxkart.exe
popd
)

if %PROCESSOR_ARCHITECTURE%==ARM (
Pushd %~dp0\bin\armv7\
supertuxkart.exe
popd
)
