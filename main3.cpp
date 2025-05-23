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

#define GET_DIE_VALUE(state, position) ((state >> ((position) * 3 + 9)) & uint64_t(0b111))
#define CLEAR_DIE_VALUE(state, position) (state & ~(uint64_t(0b111) << ((position) * 3 + 9)))
#define SET_DIE_VALUE(state, position, value) (state | ((value) << ((position) * 3 + 9)))

#define GET_DIE_SUM(state) ((state >> 37) & uint64_t(0b111111))
#define CLEAR_SUM(state) (state & ~(uint64_t(0b111111) << 37))
#define SET_SUM(state, value) (state | ((value) << 37))

#define IS_POSITION_EMPTY(state, position) ((state & (1 << (position))) == 0)
#define SET_POSITION(state, position) (state | (1 << (position)))
#define CLEAR_POSITION(state, position) (state & ~(1 << (position)))

class StateComparator
{
public:
	bool operator()(const State& lhs, const State& rhs) const
	{
		// the first should be the one with the smallest sum then the highest number of die
		const uint64_t lhs_sum = GET_DIE_SUM(lhs);
		const uint64_t rhs_sum = GET_DIE_SUM(rhs);
		if (lhs_sum != rhs_sum)
			return lhs_sum < rhs_sum;
		
		// if the sum is the same, we compare the number of die
		const int lhs_count = __builtin_popcountll(lhs & uint64_t(0b111111111));
		const int rhs_count = __builtin_popcountll(rhs & uint64_t(0b111111111));
		if (lhs_count != rhs_count)
			return lhs_count > rhs_count;
		
		// if the number of die is the same, we compare the state itself
		return lhs < rhs;
	}
};


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

uint8_t max_depth = 0;
std::unordered_map<State, uint64_t> final_states;
std::unordered_map<State, uint64_t> visited_states;

#define COUNT_ARRAY_SIZE 40
struct StateData
{
	std::array<std::array<uint32_t, COUNT_ARRAY_SIZE>, 8> counts = {{0}};
	// std::array<uint32_t, COUNT_ARRAY_SIZE> counts = {0};
};
std::map<State, StateData, StateComparator> unique_states;

struct SymmetricStateData
{
	State canonical_state;
	int symmetry;
};
std::unordered_map<State, SymmetricStateData> symmetric_states;

std::vector<std::string> log;
std::string current_log;


State set_die(State state, uint64_t position, uint64_t value)
{
	State new_state = state;

	new_state = SET_POSITION(new_state, position);

	// no need to clear the die value bits because it should be empty
	new_state = SET_DIE_VALUE(new_state, position, value);

	const uint64_t old_sum = GET_DIE_SUM(new_state);
	new_state = CLEAR_SUM(new_state);
	new_state = SET_SUM(new_state, old_sum + value);

	return new_state;
}

State remove_die(State& state, uint64_t position)
{
	State new_state = state;

	new_state = CLEAR_POSITION(new_state, position);

	const uint64_t die_value = GET_DIE_VALUE(new_state, position);
	new_state = CLEAR_DIE_VALUE(new_state, position);

	const uint64_t old_sum = GET_DIE_SUM(new_state);
	new_state = CLEAR_SUM(new_state);
	new_state = SET_SUM(new_state, old_sum - die_value);

	return new_state;
}

void print_state(State state)
{
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			const int die_value = GET_DIE_VALUE(state, i * 3 + j);
			std::cerr << die_value << " ";
		}
		std::cerr << std::endl;
	}
	std::cerr << std::endl;
}

void print_state_binary(State state)
{
	std::cerr << std::bitset<22>(state >> 45) << " ";
	std::cerr << std::bitset<6>(state >> 37 & uint64_t(0b111111)) << " ";
	for (int i = 8; i >= 0; i--)
	{
		std::cerr << std::bitset<3>((state >> (i * 3 + 9)) & uint64_t(0b111)) << " ";
	}
	std::cerr << std::bitset<9>(state & uint64_t(0b111111111)) << std::endl;
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


void insert_final_state(State new_state, uint64_t count)
{
	auto [it, is_inserted] = final_states.insert({new_state, count});
	if (!is_inserted)
	{
		it->second += count;
	}
}

void insert_final_state_full_board(State new_state, StateData &data)
{
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		uint32_t count_sum = 0;
		for (int depth = 0; depth < max_depth + 1; depth++)
		{
			count_sum += data.counts[symmetry][depth];
		}
		if (count_sum == 0)
			continue;
		State state = get_symmetric_state(new_state, symmetry);
		current_log += "-" + std::to_string(count_sum) + "*" + std::to_string(state_hash(state));
		insert_final_state(state, count_sum);
	}
}

void insert_unique_state(State new_state, StateData &data)
{
	bool no_count = true;
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		if (data.counts[symmetry][max_depth] > 0)
		{
			current_log += " fd-" + std::to_string(state_hash(new_state));
			State state = get_symmetric_state(new_state, symmetry);
			insert_final_state(state, data.counts[symmetry][max_depth]);
		}
		for (int depth = 0; depth < max_depth; depth++)
		{
			if (data.counts[symmetry][depth] > 0)
			{
				no_count = false;
				break;
			}
		}
	}
	if (no_count)
	{
		current_log += " no-count";
		return;
	}

	current_log += " " + std::to_string(state_hash(new_state));

	auto [it, is_inserted] = unique_states.insert({new_state, data});
	if (!is_inserted)
	{
		for (int symmetry = 0; symmetry < 8; symmetry++)
		{
			for (int depth = 0; depth < max_depth; depth++)
			{
				it->second.counts[symmetry][depth] += data.counts[symmetry][depth];
			}
		}
	}
	else
	{
		current_log += "*";
	}
}

void insert_possible_move(State new_state, StateData &data)
{
	auto transformed_states = get_all_symmetric_states(new_state);
	int canonical_index = get_canonical_state(transformed_states);
	State canonical_state = transformed_states[canonical_index];

	StateData new_data;
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		memcpy(
			&new_data.counts[symmetry][1],
			data.counts[symmetric_mult[canonical_index * 8 + symmetry]].data(),
			max_depth * sizeof(uint32_t)
		);
		new_data.counts[symmetry][0] = 0;
	}

	if ((canonical_state & uint64_t(0b111111111)) == uint64_t(0b111111111)) // Check if the board is full
	{
		current_log += " ff-" + std::to_string(state_hash(canonical_state));
		insert_final_state_full_board(canonical_state, new_data);
		return;
	}
	insert_unique_state(canonical_state, new_data);
}

void get_possible_moves(State state, StateData &data)
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
				insert_possible_move(new_state, data); \
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
				insert_possible_move(new_state, data); \
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
				insert_possible_move(new_state, data);
				capture_possible = true;
			}
		}
		
		if (neighbor_count < 2 || !capture_possible)
		{
			insert_possible_move(set_die(state, i, 1), data);
		}
	}

#undef two_sum
#undef three_sum
}

int compute_final_sum()
{
	int final_sum = 0;
	for (const auto& [state, count] : final_states)
	{
		const int hash = state_hash(state);
		// std::cerr << "State hash: " << hash << " count: " << count << std::endl;
		final_sum = (final_sum + hash * count);
	}
	return final_sum % MOD;
}

int main()
{
	uint32_t d;
	std::cin >> d; std::cin.ignore();
	max_depth = d;
	
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



	// std::string state_str = "060"
	// 						"222"
	// 						"161";
	// State state = 0;
	// for (int i = 0; i < 9; i++)
	// {
	// 	if (state_str[i] == '0')
	// 		continue;
	// 	state = set_die(state, i, state_str[i] - '0');
	// }
	// std::cerr << "Initial state: " << std::endl;
	// print_state(state);

	// auto states = get_all_symmetric_states(state);

	
	// int canonical_index = get_canonical_state(states);
	// State canonical = states[canonical_index];
	// std::cerr << "Canonical state: " << canonical_index << std::endl;
	// print_state(canonical);

	// // auto canonical_states = get_all_symmetric_states(canonical);
	// // for (int i = 0; i < 8; i++)
	// // {
	// // 	symmetric_states[canonical_states[i]] = {canonical, i};
	// // }
	// // std::cerr << symmetric_states[state].symmetry << std::endl;

	// // State symmetric_state = get_symmetric_state(canonical, symmetric_states[state].symmetry);
	// // print_state(symmetric_state);

	// return 0;





	if (max_depth > COUNT_ARRAY_SIZE)
	{
		std::cerr << "Max depth is too high" << std::endl;
		return 1;
	}
	
	final_states.reserve(100000);

	unique_states.insert({
		initial_state,
		{
			.counts = {{0}}
		}
	});
	unique_states[initial_state].counts[0][0] = 1;
	int iteration = 0;
	while (!unique_states.empty())
	{
		auto [state, data] = *unique_states.begin();

		unique_states.erase(unique_states.begin());

		current_log = std::to_string(state_hash(state)) + " ->";

		for (int symmetry = 0; symmetry < 8; symmetry++)
		{
			std::string depth_str;
			for (int depth = 0; depth < max_depth; depth++)
			{
				if (data.counts[symmetry][depth] > 0)
				{
					depth_str += "-" + std::to_string(depth) + "(" + std::to_string(data.counts[symmetry][depth]) + ")";
				}
			}
			if (!depth_str.empty())
			{
				current_log += " t" + std::to_string(symmetry) + depth_str;
			}
		}
		current_log += ":";
		
		get_possible_moves(state, data);

		// std::cout << current_log << std::endl;
		iteration++;
	}
	std::cerr << "Iterations: " << iteration << std::endl;

	const int final_sum = compute_final_sum();
	std::cout << final_sum << std::endl;

	{ // I have no idea why but if I remove this block, the program crashes (or at least timeout on codingame, which I believe is a crash)
		std::unordered_map<uint64_t, uint64_t> nothing;
		nothing.count(0);
	}
}
