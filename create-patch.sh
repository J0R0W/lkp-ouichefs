#!/bin/bash

git clone https://github.com/rgouicem/ouichefs.git patch/ouichefs
git clone https://github.com/J0R0W/lkp-ouichefs.git patch/lkp-ouichefs
diff -Naur --exclude mkfs/ouichefs.img --exclude .git --exclude .vscode --exclude checkpatch.pl --exclude spelling.txt --exclude Makefile patch/ouichefs --exclude .gitignore --exclude README.md --exclude docs --exclude create-patch.sh patch/lkp-ouichefs > ouichefs.patch
rm -rf patch
