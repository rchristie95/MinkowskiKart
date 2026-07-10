#!/bin/bash
cd "$(dirname "$0")"
export JAVA_HOME="/c/Program Files/JetBrains/PyCharm 2024.3.1.1/jbr"
export PATH="$JAVA_HOME/bin:$PATH"
export COMPILE_ARCH=aarch64
export BUILD_TYPE=debug
export STK_VULKAN=1
export SDK_PATH="/c/Users/robso/AppData/Local/Android/Sdk"
export NDK_PATH="/c/Users/robso/AppData/Local/Android/Sdk/ndk"
export STK_NDK_VERSION="28.1.13356709"
./make.sh
