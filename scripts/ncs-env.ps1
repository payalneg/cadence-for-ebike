# Activates the nRF Connect SDK v3.2.4 toolchain environment for this PowerShell session.
# Dot-source it before running west/cmake/nrfjprog:  . .\scripts\ncs-env.ps1
$TC = "C:\ncs\toolchains\fd21892d0f"
$env:PATH = "$TC;$TC\mingw64\bin;$TC\bin;$TC\opt\bin;$TC\opt\bin\Scripts;$TC\opt\nanopb\generator-bin;$TC\nrfutil\bin;$TC\opt\zephyr-sdk\arm-zephyr-eabi\bin;$TC\opt\zephyr-sdk\riscv64-zephyr-elf\bin;" + $env:PATH
$env:PYTHONPATH = "$TC\opt\bin;$TC\opt\bin\Lib;$TC\opt\bin\Lib\site-packages"
$env:NRFUTIL_HOME = "$TC\nrfutil\home"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "$TC\opt\zephyr-sdk"
$env:ZEPHYR_BASE = "C:\ncs\v3.2.4\zephyr"
Write-Host "NCS v3.2.4 env activated (ZEPHYR_BASE=$env:ZEPHYR_BASE)"
