If having problems with file permissions in WSL2, try following commands, replacing any c/C with the drive letter you're using (if not C):
- sudo umount /mnt/c
- sudo mount -t drvfs C: /mnt/c -o metadata