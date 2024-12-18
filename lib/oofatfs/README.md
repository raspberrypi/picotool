Sourced from https://github.com/micropython/oofatfs/tree/work-R0.13c

ooFatFs - Object Oriented version of FatFs
==========================================

This project is a modified version of the FatFs Generic FAT File System Module
by ChaN.  It is modified to make it "object oriented" such that all functions
take a pointer to the filesystem state as their first argument (or indirectly
via a file or directory structure).  This allows to create multiple,
independent FAT filesystems, and to combine this driver with other filesystem
types.

The "vendor" branch contains the original, unmodified sources and will be
updated from time-to-time when new versions become available.  Each vendor
version is tagged with a tag that mirrors the vendor's release version, for
example R0.11a.

There are then working branches that branch at a given vendor tag and apply
patches to the vendor code, for example work-R0.11a.  The patches/commits
that form a given working branch are reapplied (with conflict resolution)
to newer vendor tags to create the next working branch.

Original sources
================

From ``http://elm-chan.org/fsw/ff/00index_e.html``

For all .c and .h files in src/ directory the following processing has been
done to the original files before committing them here:
- lines endings changed to unix style
- trailing spaces removed
- tabs expanded to 4 spaces using ``expand -t 4``
