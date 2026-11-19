"""Use the same election-local clocks in the Windows and WSL upload workers."""
import sys

# The existing WSL uploader uses Python 3.8. The backport provides the
# same daylight-saving and local-date rules as the standard library on 3.9+.
if sys.version_info < (3, 9):
    from backports.zoneinfo import ZoneInfo
else:
    from zoneinfo import ZoneInfo
