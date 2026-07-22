# Activates the local nRF Connect SDK v3.2.4 environment (macOS, workspace in ~/ncs).
# Source it before running west/cmake:  . ./scripts/ncs-env.sh
export PATH="$HOME/ncs-venv/bin:$PATH"
export ZEPHYR_BASE="$HOME/ncs/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.17.4"
echo "NCS v3.2.4 env activated (ZEPHYR_BASE=$ZEPHYR_BASE)"
