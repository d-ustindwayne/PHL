"""Read and write PHL documents (.phl, .pll) and HPL tables (.hpl, .hpll)."""

import os as _os

from .errors import PHLError

# The C version is used when it was compiled. The pure Python version behaves
# the same way; set PHL_PURE_PYTHON=1 to force it.
if _os.environ.get("PHL_PURE_PYTHON"):
    _speedups = None
else:
    try:
        from . import _speedups
    except ImportError:
        _speedups = None

if _speedups is not None:
    from ._speedups import dumps, dumps_table, loads, loads_table
else:
    from ._parser import loads, loads_table
    from ._writer import dumps, dumps_table

__all__ = [
    "PHLError",
    "dump",
    "dump_table",
    "dumps",
    "dumps_table",
    "load",
    "load_table",
    "loads",
    "loads_table",
]

__version__ = "0.1.0"


def load(fp, *, coerce=True):
    """Parse a PHL document from a text file object."""
    return loads(fp.read(), coerce=coerce)


def load_table(fp, *, coerce=True):
    """Parse an HPL table from a text file object."""
    return loads_table(fp.read(), coerce=coerce)


def dump(obj, fp, *, indent=4):
    """Write a dict to a text file object as a PHL document."""
    fp.write(dumps(obj, indent=indent))


def dump_table(rows, fp, *, indent=4):
    """Write a list of dicts to a text file object as an HPL table."""
    fp.write(dumps_table(rows, indent=indent))
