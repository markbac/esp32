"""PlatformIO pre-build script: exclude PNGdec's ESP32-S3 SIMD assembly file.

PNGdec only calls that code when ARDUINO_ESP32S3_DEV is defined (the generic
dev board), so it is dead code for the Feather, and it fails to assemble
because it includes an esp-dsp header that isn't on the assembler path.
Returning None from a build middleware drops the file from the build.
"""
Import("env")  # noqa: F821 - provided by PlatformIO/SCons


def skip(node):
    return None


env.AddBuildMiddleware(skip, "*s3_simd_rgb565.S")  # noqa: F821
