#!/bin/bash

set -e

cat >tmppt.json << EOL
{
    "version": [1, 0],
    "unpartitioned": {
        "families": ["absolute"],
        "permissions": {
            "secure": "rw",
            "nonsecure": "rw",
            "bootloader": "rw"
        }
    },
    "partitions": [
        {
            "name": "Filesystem",
            "id": "0x626C6F636B646576",
            "size": "100K",
            "families": [],
            "permissions": {
                "secure": "rw",
                "nonsecure": "rw",
                "bootloader": "rw"
            }
        }
    ]
}
EOL

picotool erase || true
picotool reboot
while ! picotool info; do sleep 1; done

picotool partition create tmppt.json tmppt.bin
picotool load -x tmppt.bin
while ! picotool info; do sleep 1; done

declare -a filesystems=("littlefs" "fatfs")
for filesystem in "${filesystems[@]}"
do
    picotool bdev format --filesystem $filesystem

    echo "this is file 1" > tmpfile1.txt
    echo "this is file 2" > tmpfile2.txt
    picotool bdev cp tmpfile1.txt :/
    picotool bdev cp tmpfile2.txt :/
    picotool bdev ls | grep "tmpfile2.txt"
    picotool bdev rm tmpfile2.txt
    if picotool bdev ls | grep "tmpfile2.txt"; then
        echo "Error: tmpfile2.txt was not deleted"
        exit 1
    fi
    picotool bdev cat tmpfile1.txt
    picotool bdev cat tmpfile1.txt > tmpfile1.txt.device

    if ! cmp -s tmpfile1.txt tmpfile1.txt.device; then
        echo "Error: tmpfile1.txt content has changed"
        exit 1
    fi

    echo "this is new file 2" > tmpfile2.txt
    picotool bdev cp tmpfile2.txt :/
    picotool bdev cat tmpfile2.txt
    picotool bdev cat tmpfile2.txt > tmpfile2.txt.device

    if ! cmp -s tmpfile2.txt tmpfile2.txt.device; then
        echo "Error: tmpfile2.txt content has changed"
        exit 1
    fi
done

rm tmpfile1.txt
rm tmpfile1.txt.device
rm tmpfile2.txt
rm tmpfile2.txt.device
rm tmppt.json
rm tmppt.bin
