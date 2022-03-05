If having problems with file permissions in WSL2, try following commands, replacing any c/C with the drive letter you're using (if not C):
- sudo umount /mnt/c
- sudo mount -t drvfs C: /mnt/c -o metadata

To quickly set up the system, copy all subfolders in the "Archives" subfolder, and paste them into its parent "analysis" folder (i.e. the folder this Readme is in.)
Note that it's still recommended to run the whole generation process to keep everything up to date, as the archived files may not include recent changes.