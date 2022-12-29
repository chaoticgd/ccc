#!/bin/bash
set -e
for elf in test/*; do
	echo "**** TESTING FILE '$elf' ****";
	./bin/stdump print_types $elf;
done
