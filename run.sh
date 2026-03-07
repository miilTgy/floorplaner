#!/bin/bash

make clean
make run INPUT=samples/sample_1.txt T=1 | tee output1.log
make run INPUT=samples/sample_2.txt T=1 | tee output2.log
make run INPUT=samples/sample_3.txt T=1 | tee output3.log
make run INPUT=samples/sample_4.txt T=1 | tee output4.log
make run INPUT=samples/sample_5.txt T=1 | tee output5.log