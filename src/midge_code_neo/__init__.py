"""
midge-code-neo: New Implementation of the firmware-software solution for the Midge Sociometric badges
"""  # noqa: E501, RUF100

from __future__ import annotations

from importlib.metadata import Distribution, version


def _is_editable() -> bool:
    dist = Distribution.from_name("midge-code-neo")

    try:
        editable = dist.origin.dir_info.editable
    except AttributeError:
        editable = False

    if not isinstance(editable, bool):
        return False

    return editable


if _is_editable():
    # Development way of getting the version, it is dynamically updated on each call
    try:
        # setuptools_scm is not included in the runtime virtual environment
        # importing it raises an ImportError,
        # it also fails in the gitlab CI with a UserWarning error
        from setuptools_scm import get_version

        # This will fail with LookupError if Git is not installed
        __version__ = get_version(root="../..", relative_to=__file__)
    except ImportError, LookupError, UserWarning:
        __version__ = version(__name__)
else:
    # Get the version as specified by the wheel
    __version__ = version(__name__)

__all__ = ("__version__",)
