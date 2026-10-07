# PlatformIO pre-script: install Python packages the bundled esptool needs.
#
# tool-esptoolpy 4.11 imports intelhex when creating bootloader.bin, but the
# platform does not install it into PlatformIO's own Python environment.
import importlib.util
import subprocess

Import("env")  # noqa: F821 - provided by SCons

if importlib.util.find_spec("intelhex") is None:
    subprocess.check_call([env.subst("$PYTHONEXE"), "-m", "pip", "install", "--quiet", "intelhex"])  # noqa: F821
