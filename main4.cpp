#undef _GLIBCXX_DEBUG
#pragma GCC optimize "Ofast,unroll-loops,omit-frame-pointer,inline"
#pragma GCC option("arch=native", "tune=native", "no-zero-upper")
//ifndef POPCNT
#pragma GCC target("movbe,aes,pclmul,avx,avx2,f16c,fma,sse3,ssse3,sse4.1,sse4.2,rdrnd,popcnt,bmi,bmi2,lzcnt")
//#endif

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <queue>

typedef uint64_t State;
typedef uint64_t Count;
/*
0 to 8 bits: tell if the position is empty or not
9 to 36 bits: tell the value of the die on this position, with 3 bits for each position
37 to 44 bits: tell the sum of the die on this position

					   die sum							is there a die on this position ?
					   ------									  ---------
0000000000000000000000 000000 000 000 000 000 000 000 000 000 000 000000000
							  -----------------------------------
								 die value for each position

positions:
0 1 2
3 4 5
6 7 8
*/

#define GET_DIE_VALUE(state, position) ((state >> (position * 3 + 9)) & uint64_t(0b111))
#define CLEAR_DIE_VALUE(state, position) (state & ~(uint64_t(0b111) << (position * 3 + 9)))
#define SET_DIE_VALUE(state, position, value) (state | ((value) << (position * 3 + 9)))

#define IS_POSITION_EMPTY(state, position) ((state & (1 << position)) == 0)
#define SET_POSITION(state, position) (state | (1 << position))
#define CLEAR_POSITION(state, position) (state & ~(1 << position))

// masks to get the neighbor die presence (first 9 bits) for each position
const std::array<State, 9> neighbors_mask = {
//  876543210
	0b000001010, // 0
	0b000010101, // 1
	0b000100010, // 2
	0b001010001, // 3
	0b010101010, // 4
	0b100010100, // 5
	0b010001000, // 6
	0b101010000, // 7
	0b010100000  // 8
};

const std::array<std::array<uint8_t, 9>, 8> symmetric_positions = {
	// 0 1 2
	// 3 4 5
	// 6 7 8
	{
		{0, 1, 2, 3, 4, 5, 6, 7, 8}, // left top (I)
		{6, 7, 8, 3, 4, 5, 0, 1, 2}, // left bottom (V)
		{2, 1, 0, 5, 4, 3, 8, 7, 6}, // right top (H)
		{8, 7, 6, 5, 4, 3, 2, 1, 0}, // right bottom (VH)
		{0, 3, 6, 1, 4, 7, 2, 5, 8}, // top left (D)
		{6, 3, 0, 7, 4, 1, 8, 5, 2}, // bottom left (DV)
		{2, 5, 8, 1, 4, 7, 0, 3, 6}, // top right (DH)
		{8, 5, 2, 7, 4, 1, 6, 3, 0}, // bottom right (DVH)
	}
};

const std::array<uint8_t, 64> symmetric_mult = {
	0, 1, 2, 3, 4, 5, 6, 7,
	1, 0, 3, 2, 6, 7, 4, 5,
	2, 3, 0, 1, 5, 4, 7, 6,
	3, 2, 1, 0, 7, 6, 5, 4,
	4, 5, 6, 7, 0, 1, 2, 3,
	5, 4, 7, 6, 2, 3, 0, 1,
	6, 7, 4, 5, 1, 0, 3, 2,
	7, 6, 5, 4, 3, 2, 1, 0
};

constexpr uint64_t MOD = 1ULL << 30;

int max_depth;
int current_depth;
std::unordered_map<State, std::array<Count, 8>> states_to_process;
std::unordered_map<State, std::array<Count, 8>> new_states_to_process;
std::array<std::array<uint32_t, 9>, 8> symmetric_final_states = {0};
int final_sum = 0;

std::string current_log;

State set_die(State state, uint64_t position, uint64_t value)
{
	State new_state = state;

	new_state = SET_POSITION(new_state, position);

	// no need to clear the die value bits because it should be empty
	new_state = SET_DIE_VALUE(new_state, position, value);

	return new_state;
}

State remove_die(State& state, uint64_t position)
{
	State new_state = state;

	new_state = CLEAR_POSITION(new_state, position);

	const uint64_t die_value = GET_DIE_VALUE(new_state, position);
	new_state = CLEAR_DIE_VALUE(new_state, position);

	return new_state;
}

void print_state(State state)
{
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			const int die_value = GET_DIE_VALUE(state, i * 3 + j);
			std::cout << die_value << " ";
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}

int state_hash(State state)
{
	int state_hash = 0;
	for (int i = 0; i < 9; i++)
	{
		state_hash = state_hash * 10 + GET_DIE_VALUE(state, i);
	}
	return state_hash;
}


State get_symmetric_state(State state, int symmetry)
{
	State new_state = 0;
	for (int i = 0; i < 9; i++)
	{
		const int new_position = symmetric_positions[symmetry][i];
		const int die_value = GET_DIE_VALUE(state, i);
		if (die_value == 0)
			continue;
		new_state = set_die(new_state, new_position, die_value);
	}
	return new_state;
}

std::array<State, 8> get_all_symmetric_states(State state)
{
	std::array<State, 8> symmetric_states;
	for (int i = 0; i < 8; i++)
	{
		symmetric_states[i] = get_symmetric_state(state, i);
	}
	return symmetric_states;
}

int get_canonical_state(std::array<State, 8> &symmetric_states)
{
	int canonical_state = 0;
	for (int i = 1; i < 8; i++)
	{
		if (symmetric_states[i] < symmetric_states[canonical_state])
		{
			canonical_state = i;
		}
	}
	return canonical_state;
}


void insert_final_state(State new_state, std::array<Count, 8> & counts)
{
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		if (counts[symmetry] == 0)
			continue;
		
			
		State symmetric_state = get_symmetric_state(new_state, symmetry);
		const int hash = state_hash(symmetric_state);
		final_sum += hash * counts[symmetry];
	}
}

void insert_new_state_to_process(State new_state, std::array<Count, 8> & counts)
{
	if (current_depth == max_depth - 1)
	{
		// current_log += " fd-" + std::to_string(state_hash(new_state));
		insert_final_state(new_state, counts);
		return;
	}

	// current_log += " " + std::to_string(state_hash(new_state));
	auto [it, is_inserted] = new_states_to_process.insert({new_state, counts});
	if (!is_inserted)
	{
		for (int symmetry = 0; symmetry < 8; symmetry++)
		{	
			it->second[symmetry] += counts[symmetry];
		}
	}
	// else
	// {
	// 	current_log += "*";
	// }

}

void insert_possible_move(State new_state, const std::array<Count, 8> & counts)
{
	auto transformed_states = get_all_symmetric_states(new_state);
	int canonical_index = get_canonical_state(transformed_states);
	State canonical_state = transformed_states[canonical_index];

	std::array<Count, 8> new_counts;
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		new_counts[symmetry] = counts[symmetric_mult[canonical_index * 8 + symmetry]];
	}

	if ((canonical_state & uint64_t(0b111111111)) == uint64_t(0b111111111)) // Check if the board is full
	{
		// current_log += " ff-" + std::to_string(state_hash(canonical_state));
		insert_final_state(canonical_state, new_counts);
		return;
	}
	insert_new_state_to_process(canonical_state, new_counts);
}

void get_possible_moves(State state, const std::array<Count, 8> & counts)
{
	for (int i = 0; i < 9; i++)
	{
		if (!IS_POSITION_EMPTY(state, i))
			continue;

		const State neighbor_mask = state & neighbors_mask[i];
		const int neighbor_count = __builtin_popcountll(neighbor_mask);
		bool capture_possible = false;

#define two_sum(i0, n0, i1, n1) \
		{ \
			const int sum = n0 + n1; \
			if (sum <= 6) \
			{ \
				State new_state = remove_die(state, i0); \
				new_state = remove_die(new_state, i1); \
				new_state = set_die(new_state, i, sum); \
				insert_possible_move(new_state, counts); \
				capture_possible = true; \
			} \
		}

#define three_sum(i0, n0, i1, n1, i2, n2) \
		{ \
			const int sum = n0 + n1 + n2; \
			if (sum <= 6) \
			{ \
				State new_state = remove_die(state, i0); \
				new_state = remove_die(new_state, i1); \
				new_state = remove_die(new_state, i2); \
				new_state = set_die(new_state, i, sum); \
				insert_possible_move(new_state, counts); \
				capture_possible = true; \
			} \
		}

		if (neighbor_count == 2)
		{
			const int first_neighbor_index = __builtin_ctzll(neighbor_mask);
			const int second_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index));
			const int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
			const int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);

			two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
		}
		else if (neighbor_count == 3)
		{
			const int first_neighbor_index = __builtin_ctzll(neighbor_mask);
			const int second_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index));
			const int third_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index) & ~(1 << second_neighbor_index));
			const int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
			const int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);
			const int third_die_value = GET_DIE_VALUE(state, third_neighbor_index);

			two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
			two_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value);
			two_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
			three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
		}
		else if (neighbor_count == 4)
		{
			const int first_neighbor_index = __builtin_ctzll(neighbor_mask);
			const int second_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index));
			const int third_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index) & ~(1 << second_neighbor_index));
			const int fourth_neighbor_index = __builtin_ctzll(neighbor_mask & ~(1 << first_neighbor_index) & ~(1 << second_neighbor_index) & ~(1 << third_neighbor_index));
			const int first_die_value = GET_DIE_VALUE(state, first_neighbor_index);
			const int second_die_value = GET_DIE_VALUE(state, second_neighbor_index);
			const int third_die_value = GET_DIE_VALUE(state, third_neighbor_index);
			const int fourth_die_value = GET_DIE_VALUE(state, fourth_neighbor_index);

			two_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value);
			two_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value);
			two_sum(first_neighbor_index, first_die_value, fourth_neighbor_index, fourth_die_value);
			two_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
			two_sum(second_neighbor_index, second_die_value, fourth_neighbor_index, fourth_die_value);
			two_sum(third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);
			three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, third_neighbor_index, third_die_value);
			three_sum(first_neighbor_index, first_die_value, second_neighbor_index, second_die_value, fourth_neighbor_index, fourth_die_value);
			three_sum(first_neighbor_index, first_die_value, third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);
			three_sum(second_neighbor_index, second_die_value, third_neighbor_index, third_die_value, fourth_neighbor_index, fourth_die_value);

			const int sum = first_die_value + second_die_value + third_die_value + fourth_die_value;
			if (sum <= 6)
			{
				State new_state = remove_die(state, first_neighbor_index);
				new_state = remove_die(new_state, second_neighbor_index);
				new_state = remove_die(new_state, third_neighbor_index);
				new_state = remove_die(new_state, fourth_neighbor_index);
				new_state = set_die(new_state, i, sum);
				insert_possible_move(new_state, counts);
				capture_possible = true;
			}
		}
		
		if (neighbor_count < 2 || !capture_possible)
		{
			insert_possible_move(set_die(state, i, 1), counts);
		}
	}
}

int compute_final_sum()
{
	return final_sum % MOD;
}

int main()
{
	std::cin >> max_depth; std::cin.ignore();
	
	State initial_state = 0;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			State value;
			std::cin >> value; std::cin.ignore();
			if (value == 0)
				continue;
			initial_state = set_die(initial_state, i * 3 + j, value);
		}
	}
	
	new_states_to_process.reserve(100000);
	states_to_process.insert({initial_state, {0}});
	states_to_process[initial_state][0] = 1;

	int iteration = 0;
	for (current_depth = 0; current_depth < max_depth; current_depth++)
	{
		if (states_to_process.empty())
			break;
		
		// std::cout << "depth: " << (current_depth+1) << "/" << max_depth << std::endl;
		// std::cout << "states to process: " << states_to_process.size() << std::endl;
		
		new_states_to_process.clear();
		for (const auto& [state, counts] : states_to_process)
		{
			// current_log = std::to_string(state_hash(state));
			// for (int i = 0; i < 8; i++)
			// {
			// 	current_log += " " + std::to_string(counts[i]);
			// }
			// current_log += ":";

			get_possible_moves(state, counts);

			// std::cout << current_log << std::endl;
			iteration++;
		}
		states_to_process = std::move(new_states_to_process);

		// std::cout << std::endl;
	}
	// std::cout << "Iterations: " << iteration << std::endl;

	const int final_sum = compute_final_sum();
	std::cout << final_sum << std::endl;
}
