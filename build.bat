@echo off
rem FTLCheck build script (no make needed) — run from the ftlcheck folder.
setlocal
if not exist bin mkdir bin

echo Compiling FTLCheck...
g++ -std=c++17 -Wall -Wextra -O2 -g ^
    src\nand\nand_device.cpp ^
    src\ftl\ftl.cpp ^
    src\framework\test_registry.cpp ^
    src\framework\test_runner.cpp ^
    src\framework\report.cpp ^
    src\tests\basic_io_tests.cpp ^
    src\tests\gc_tests.cpp ^
    src\tests\wear_tests.cpp ^
    src\tests\bad_block_tests.cpp ^
    src\tests\power_loss_tests.cpp ^
    src\main.cpp ^
    -o bin\ftlcheck.exe || goto :err

echo.
echo Build OK. Try:
echo   bin\ftlcheck.exe demo
echo   bin\ftlcheck.exe run --report bin\report.html
exit /b 0

:err
echo BUILD FAILED
exit /b 1
