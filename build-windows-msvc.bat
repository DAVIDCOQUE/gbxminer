@echo off
REM Windows build: everything compiled with MSVC, CUDA host code included.
REM
REM The MSYS2/MinGW recipe cannot work here: nvcc on Windows only accepts MSVC
REM as its host compiler, so the .cu objects use the MSVC C++ ABI and CRT while
REM the MinGW objects use their own. The mangling never matches, and even when
REM it links the two C runtimes crash the process during startup. Using one
REM compiler for everything removes both problems.
REM
REM Run from a "x64 Native Tools Command Prompt for VS 2022", or call
REM vcvars64.bat first. Override CUDA_DIR / GPU_ARCH on the command line:
REM
REM   build-windows-msvc.bat
REM   set GPU_ARCH=compute_89 & build-windows-msvc.bat
REM
setlocal
if "%CUDA_DIR%"=="" set CUDA_DIR=C:\cuda
if "%GPU_ARCH%"=="" set GPU_ARCH=compute_86
for /f %%A in ("%GPU_ARCH%") do set SM=sm_%%A
set SM=%GPU_ARCH:compute_=sm_%

set REPO=%~dp0
set OBJ=%REPO%obj-msvc
if exist "%OBJ%" rmdir /s /q "%OBJ%"
mkdir "%OBJ%" & mkdir "%OBJ%\sph" & mkdir "%OBJ%\js"

where cl.exe >nul 2>&1 || (echo error: cl.exe not on PATH, run vcvars64.bat first & exit /b 1)
if not exist "%CUDA_DIR%\bin\nvcc.exe" (echo error: no nvcc under %CUDA_DIR% & exit /b 1)

set OSSL=%REPO%compat\curl-for-windows\openssl\openssl\include
set INC=/I "%REPO%." /I "%REPO%compat" /I "%REPO%compat\jansson" /I "%REPO%compat\curl-for-windows\curl\include" /I "%OSSL%" /I "%REPO%compat\pthreads" /I "%CUDA_DIR%\include"
set DEF=/DWIN32 /DNDEBUG /D_CONSOLE /D_CRT_SECURE_NO_WARNINGS /DCURL_STATICLIB /DUSE_WRAPNVML /DHAVE_CONFIG_H
set CF=/nologo /c /O2 /EHsc /MT /W1 /GS-

cd /d "%REPO%"

echo [1/5] host sources
cl %CF% %DEF% %INC% /Fo"%OBJ%\\" ^
   crc32.c hefty1.c gbxminer.cpp pools.cpp util.cpp bench.cpp bignum.cpp ^
   api.cpp hashlog.cpp nvml.cpp stats.cpp sysinfos.cpp cuda.cpp nvsettings.cpp ^
   stubs.cpp bitcoin_solo.cpp solo_script.cpp nvapi.cpp equi\equi-stratum.cpp ^
   compat\winansi.c compat\gettimeofday.c compat\msvc_legacy_crt.c || exit /b 1

echo [2/5] getopt
cl %CF% %DEF% /I "%REPO%compat\getopt" %INC% /Fo"%OBJ%\\" compat\getopt\getopt_long.c || exit /b 1

echo [3/5] sph
cl %CF% %DEF% %INC% /Fo"%OBJ%\sph\\" ^
   sph\blake.c sph\blake2b.c sph\blake2s.c sph\bmw.c sph\cubehash.c sph\echo.c ^
   sph\fugue.c sph\groestl.c sph\hamsi.c sph\hamsi_helper.c sph\haval.c sph\jh.c ^
   sph\keccak.c sph\luffa.c sph\ripemd.c sph\sha2.c sph\sha2big.c sph\shabal.c ^
   sph\shavite.c sph\simd.c sph\skein.c sph\sph_sha2.c sph\streebog.c sph\whirlpool.c || exit /b 1

echo [4/5] jansson
cl %CF% %DEF% %INC% /Fo"%OBJ%\js\\" compat\jansson\*.c || exit /b 1

echo [5/5] CUDA (%GPU_ARCH% / %SM%)
set NVF=-gencode=arch=%GPU_ARCH%,code=%SM% -I"%CUDA_DIR%\include" -I. -I"%REPO%compat" ^
 -I"%REPO%compat\pthreads" -I"%REPO%compat\jansson" -I"%REPO%compat\curl-for-windows\curl\include" ^
 -O3 -ccbin cl.exe --std=c++14 -Xcompiler /MT -Xcompiler /GS- -D_FORCE_INLINES -DWIN32 ^
 -DUSE_WRAPNVML -DHAVE_CONFIG_H
for %%F in ("cuda_checkhash.cu" "sha256\sha256d.cu" "sha256\cuda_sha256d.cu" ^
            "sha256\sha256t.cu" "sha256\cuda_sha256t.cu") do (
  "%CUDA_DIR%\bin\nvcc.exe" %NVF% --maxrregcount=128 -o "%OBJ%\cu_%%~nF.obj" -c "%%~F" || exit /b 1
)

echo link
link /nologo /OUT:"%REPO%gbxminer.exe" /SUBSYSTEM:CONSOLE /MACHINE:X64 ^
  "%OBJ%\*.obj" "%OBJ%\sph\*.obj" "%OBJ%\js\*.obj" ^
  /LIBPATH:"%CUDA_DIR%\lib\x64" ^
  /LIBPATH:"%REPO%compat\curl-for-windows\out\x64\Release\lib" ^
  /LIBPATH:"%REPO%compat\pthreads\x64" /LIBPATH:"%REPO%compat\nvapi\amd64" ^
  cudart_static.lib cuda.lib libcurl.x64.lib openssl.x64.lib zlib.x64.lib ^
  pthreadVC2.lib nvapi64.lib ws2_32.lib wldap32.lib winmm.lib advapi32.lib ^
  user32.lib kernel32.lib crypt32.lib normaliz.lib shell32.lib ole32.lib || exit /b 1

echo.
echo built %REPO%gbxminer.exe
endlocal
