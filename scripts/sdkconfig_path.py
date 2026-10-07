# PlatformIO pre-script: give ESP-IDF an absolute path to the project sdkconfig.
#
# A relative board_build.esp-idf.sdkconfig_path works for the app (CMake runs
# from the project) but not for the bootloader subproject, which then silently
# falls back to ESP-IDF defaults: 2 MB flash, 40 MHz, no app rollback.
import os

Import("env")  # noqa: F821 - provided by SCons

env.BoardConfig().update("build.esp-idf.sdkconfig_path", os.path.join(env.subst("$PROJECT_DIR"), "sdkconfig"))  # noqa: F821
