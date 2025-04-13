#! /bin/bash

total_time=0
count=$2
test_file=(2168.txt 4154.txt 4956.txt 6044.txt 93190.txt 94596.txt 316712.txt)

# Compile the C++ program
g++ -std=c++20 -o test_prog $1

for file in "${test_file[@]}"
do
	time=0
	for i in $(seq 1 $count)
	do
		start_time=$(($(date +%s%N)/1000000))
		./test_prog < "tests/$file" > /dev/null 2> /dev/null
		end_time=$(($(date +%s%N)/1000000))
		elapsed_time=$((end_time - start_time))
		time=$((time + elapsed_time))
	done
	average_time=$((time / count))
	echo "Average time: $average_time ms"

	# Add the average time to the total time
	total_time=$((total_time + average_time))
done

echo "Total time: $total_time ms"

rm test_prog