rm disk1
#rm clean_disk
cd ../..
cp disk1 sample_tests/sample_test_3
#cp clean_disk sample_tests/sample_test_4
cd sample_tests/sample_test_3
gcc -Wall -o fs FileSystem.c
./fs input3