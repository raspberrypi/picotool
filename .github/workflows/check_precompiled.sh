#!/bin/bash

# Pass updated files in $updated_files environment variable

# enc_bootloader
if ! echo "$updated_files" | grep -q "^enc_bootloader/.*\.elf$"; then
    echo "Checking enc_bootloader files for modifications as ELFs have not been updated"
    for file in enc_bootloader/*; do
        if [[ "$file" == "enc_bootloader/CMakeLists.txt" || "$file" == "enc_bootloader/BUILD.bazel" ]]; then
            continue
        fi
        if [[ "$updated_files" == *"$file"* ]]; then
            echo "File $file is in the PR but enc_bootloader ELFs have not been updated"
            exit 1
        fi
    done
fi

# picoboot_flash_id
if ! echo "$updated_files" | grep -q "^picoboot_flash_id/.*\.bin$"; then
    echo "Checking picoboot_flash_id files for modifications as BINs have not been updated"
    for file in picoboot_flash_id/*; do
        if [[ "$file" == "picoboot_flash_id/CMakeLists.txt" || "$file" == "picoboot_flash_id/BUILD.bazel" ]]; then
            continue
        fi
        if [[ "$updated_files" == *"$file"* ]]; then
            echo "File $file is in the PR but flash_id BINs have not been updated"
            exit 1
        fi
    done
fi

# xip_ram_perms
if ! echo "$updated_files" | grep -q "^xip_ram_perms/.*\.elf$"; then
    echo "Checking xip_ram_perms files for modifications as ELFs have not been updated"
    for file in xip_ram_perms/*; do
        if [[ "$file" == "xip_ram_perms/CMakeLists.txt" || "$file" == "xip_ram_perms/BUILD.bazel" ]]; then
            continue
        fi
        if [[ "$updated_files" == *"$file"* ]]; then
            echo "File $file is in the PR but xip_ram_perms ELFs have not been updated"
            exit 1
        fi
    done
fi