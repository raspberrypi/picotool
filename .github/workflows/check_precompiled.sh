#!/bin/bash

# Pass updated files in $updated_files environment variable

# enc_bootloader
echo "$updated_files" | grep -q "enc_bootloader/.*\.elf"
enc_bootloader_elf_not_updated=$?
if [ $enc_bootloader_elf_not_updated -eq 1 ]; then
    echo "Checking enc_bootloader files for modifications as ELFs have not been updated"
    for file in enc_bootloader/*; do
        if echo "$file" | grep -q "CMakeLists.txt" || echo "$file" | grep -q "BUILD.bazel"; then
            continue
        fi
        if echo "$updated_files" | grep -q "$file"; then
            echo "File $file is in the PR but enc_bootloader ELFs have not been updated"
            exit 1
        fi
    done
fi

# picoboot_flash_id
echo "$updated_files" | grep -q "picoboot_flash_id/.*\.bin"
flash_id_bin_not_updated=$?
if [ $flash_id_bin_not_updated -eq 1 ]; then
    echo "Checking picoboot_flash_id files for modifications as BINs have not been updated"
    for file in picoboot_flash_id/*; do
        if echo "$file" | grep -q "CMakeLists.txt" || echo "$file" | grep -q "BUILD.bazel"; then
            continue
        fi
        if echo "$updated_files" | grep -q "$file"; then
            echo "File $file is in the PR but flash_id BINs have not been updated"
            exit 1
        fi
    done
fi

# xip_ram_perms
echo "$updated_files" | grep -q "xip_ram_perms/.*\.elf"
xip_ram_perms_elf_not_updated=$?
if [ $xip_ram_perms_elf_not_updated -eq 1 ]; then
    echo "Checking xip_ram_perms files for modifications as ELFs have not been updated"
    for file in xip_ram_perms/*; do
        if echo "$file" | grep -q "CMakeLists.txt" || echo "$file" | grep -q "BUILD.bazel"; then
            continue
        fi
        if echo "$updated_files" | grep -q "$file"; then
            echo "File $file is in the PR but xip_ram_perms ELFs have not been updated"
            exit 1
        fi
    done
fi