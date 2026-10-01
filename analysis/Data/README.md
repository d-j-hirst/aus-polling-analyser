# Analysis Source Data

This directory contains the source tables used by the Python analysis tools.
Keeping their dates in a consistent format makes the same inputs readable
across machines and regional settings.

The tools read dates in `yyyy-mm-dd` format. When editing CSV files in Excel,
check that the dates retain that format before saving: Excel can convert them
to the default local format. To preserve the dates, either:
* Open in a text editor such as Notepad++, which will not change the format, or
* Change the default date format on your system to yyyy-mm-dd following the instructions [here](https://techcommunity.microsoft.com/t5/excel/changing-default-date-format-for-csv-download/m-p/1642250), or
* After editing cells in Excel, manually select the dates and adjust them to the correct format
