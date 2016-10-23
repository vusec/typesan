#!/bin/bash

set -e

exec 5> ubench-gen.h
exec 6> ubench-gen-inc.h

echo "class BaseClass {"   >&5
echo "};"                  >&5
echo "class NestedClass {" >&5
echo "long value;"         >&5
echo "};"                  >&5

size=1
for sizelog in `seq 1 10`; do
	echo "class TestSimple${sizelog}Class : public BaseClass {" >&5
	for i in `seq 1 "$size"`; do
		echo "long member$i;"            >&5
	done
	echo "};"                                >&5
	echo "class TestNested${sizelog}Class : public BaseClass {" >&5
	for i in `seq 1 "$size"`; do
		echo "NestedClass member$i;"     >&5
	done
	echo "};"                                >&5

	echo "TESTSIZE($sizelog, 0, TestSimple${sizelog}Class)" >&6
	echo "TESTSIZE($sizelog, 1, TestNested${sizelog}Class)" >&6
	
	size="`expr "$size" \* 2`"
done
