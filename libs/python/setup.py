"""Builds the optional C version of the parser and writer.

The project's metadata is in pyproject.toml. Everything still installs and
works without a C compiler; the pure Python version is used then.
"""

import shutil
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

HERE = Path(__file__).resolve().parent
LIBRARY = HERE.parent / "c"
NATIVE = "native"


def copy_library():
    """Copies the C library next to this file, so source packages include it."""
    if not (LIBRARY / "src").exists():
        return
    target = HERE / NATIVE
    target.mkdir(exist_ok=True)
    for source in [LIBRARY / "include" / "phl.h", *sorted((LIBRARY / "src").glob("*"))]:
        shutil.copyfile(source, target / source.name)


class OptionalBuild(build_ext):
    """Falls back to pure Python, whatever stops the compiler from working."""

    def run(self):
        try:
            super().run()
        except Exception as error:
            print(f"phl: not building the C version ({error}); using pure Python")

    def build_extension(self, ext):
        try:
            super().build_extension(ext)
        except Exception as error:
            print(f"phl: not building the C version ({error}); using pure Python")


copy_library()

setup(
    ext_modules=[
        Extension(
            "phl._speedups",
            sources=[
                "src/phl/_speedups.c",
                f"{NATIVE}/value.c",
                f"{NATIVE}/parser.c",
                f"{NATIVE}/writer.c",
            ],
            include_dirs=[NATIVE],
            # One compiled file then works on every Python from 3.8 on.
            define_macros=[("Py_LIMITED_API", "0x03080000")],
            py_limited_api=True,
            optional=True,
        )
    ],
    cmdclass={"build_ext": OptionalBuild},
    options={"bdist_wheel": {"py_limited_api": "cp38"}},
)
