#! /bin/sh

total_time=0
count=$2

# Compile the C++ program
g++ -std=c++17 -o test_prog $1

for i in $(seq 1 $count)
do
	start_time=$(($(date +%s%N)/1000000))
	./test_prog < test.txt 2> /dev/null
	end_time=$(($(date +%s%N)/1000000))
	elapsed_time=$((end_time - start_time))
	total_time=$((total_time + elapsed_time))
done
average_time=$((total_time / count))
echo "Average time taken for $count runs: $average_time ms"

rm test_prog