for elf in test/*; do
	echo "**** TESTING FILE '$elf' ****";
	./bin/stdump print_cpp $elf;
done
