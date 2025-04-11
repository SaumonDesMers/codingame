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
#include <bitset>
#include <cstdint>
#include <cstring>
#include <map>

typedef uint32_t State;
typedef uint32_t Count;

#define GET_DIE_VALUE(state, position) ((state >> ((position) * 3)) & 0b111)
#define CLEAR_DIE_VALUE(state, position) (state & ~(0b111 << ((position) * 3)))
#define SET_DIE_VALUE(state, position, value) (state | ((value) << ((position) * 3)))

#define IS_POSITION_EMPTY(state, position) ((state & (0b111 << ((position) * 3))) == 0)

const std::array<State, 9> neighbors_mask = {
	0b000'000'000'000'000'111'000'111'000, // 0
	0b000'000'000'000'111'000'111'000'111, // 1
	0b000'000'000'111'000'000'000'111'000, // 2
	0b000'000'111'000'111'000'000'000'111, // 3
	0b000'111'000'111'000'111'000'111'000, // 4
	0b111'000'000'000'111'000'111'000'000, // 5
	0b000'111'000'000'000'111'000'000'000, // 6
	0b111'000'111'000'111'000'000'000'000, // 7
	0b000'111'000'111'000'000'000'000'000, // 8
};

const std::array<uint8_t, 72> symmetric_positions = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, // left top (I)
	6, 7, 8, 3, 4, 5, 0, 1, 2, // left bottom (V)
	2, 1, 0, 5, 4, 3, 8, 7, 6, // right top (H)
	8, 7, 6, 5, 4, 3, 2, 1, 0, // right bottom (VH)
	0, 3, 6, 1, 4, 7, 2, 5, 8, // top left (D)
	6, 3, 0, 7, 4, 1, 8, 5, 2, // bottom left (DV)
	2, 5, 8, 1, 4, 7, 0, 3, 6, // top right (DH)
	8, 5, 2, 7, 4, 1, 6, 3, 0, // bottom right (DVH)
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

constexpr State MASK_0 = 0x7;
constexpr State MASK_1 = 0x7 << 3;
constexpr State MASK_2 = 0x7 << 6;
constexpr State MASK_3 = 0x7 << 9;
constexpr State MASK_4 = 0x7 << 12;
constexpr State MASK_5 = 0x7 << 15;
constexpr State MASK_6 = 0x7 << 18;
constexpr State MASK_7 = 0x7 << 21;
constexpr State MASK_8 = 0x7 << 24;

int max_depth;
int current_depth;
std::unordered_map<State, std::array<Count, 8>> states_to_process;
std::unordered_map<State, std::array<Count, 8>> new_states_to_process;
uint32_t final_sum = 0;

// std::string log;

State set_die(const State state, const uint64_t position, const uint64_t value)
{
	return SET_DIE_VALUE(state, position, value);
}

State remove_die(const State state, const uint64_t position)
{
	return CLEAR_DIE_VALUE(state, position);
}

void print_state(const State state)
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

int state_hash(const State state)
{
	int state_hash = 0;
	for (int i = 0; i < 9; i++)
	{
		state_hash = state_hash * 10 + GET_DIE_VALUE(state, i);
	}
	return state_hash;
}

State create_state(const std::string & state_str)
{
	if (state_str.length() != 9)
	{
		std::cerr << "Invalid state string length" << std::endl;
		return 0;
	}
	State state = 0;
	for (int i = 0; i < 9; i++)
	{
		state = SET_DIE_VALUE(state, i, state_str[i] - '0');
	}
	return state;
}


State get_symmetric_state(const State state, const int symmetry)
{
	#define VERTCAL_FLIP(state) \
		((state & 0b111'111'111'000'000'000'000'000'000) >> 18) | \
		((state & 0b000'000'000'000'000'000'111'111'111) << 18) | \
		(state & 0b000'000'000'111'111'111'000'000'000)

	#define HORIZONTAL_FLIP(state) \
		((state & 0b000'000'111'000'000'111'000'000'111) << 6) | \
		((state & 0b111'000'000'111'000'000'111'000'000) >> 6) | \
		(state & 0b000'111'000'000'111'000'000'111'000)

	#define DIAGONAL_FLIP(state) \
		(state & MASK_0) | \
		((state & MASK_1) << 6) | \
		((state & MASK_2) << 12) | \
		((state & MASK_3) >> 6) | \
		(state & MASK_4) | \
		((state & MASK_5) << 6) | \
		((state & MASK_6) >> 12) | \
		((state & MASK_7) >> 6) | \
		(state & MASK_8)

	#define DVH_FLIP(state) \
		((state & MASK_0) << 24) | \
		((state & MASK_1) << 12) | \
		(state & MASK_2) | \
		((state & MASK_3) << 12) | \
		(state & MASK_4) | \
		((state & MASK_5) >> 12) | \
		(state & MASK_6) | \
		((state & MASK_7) >> 12) | \
		((state & MASK_8) >> 24)

	switch (symmetry)
	{
		case 0: return state; // I
		case 1: return VERTCAL_FLIP(state); // V
		case 2: return HORIZONTAL_FLIP(state); // H
		case 3: { const State v = VERTCAL_FLIP(state); return HORIZONTAL_FLIP(v); }; // VH
		case 4: return DIAGONAL_FLIP(state); // D
		case 5: { const State d = DIAGONAL_FLIP(state); return VERTCAL_FLIP(d); }; // DV
		case 6: { const State d = DIAGONAL_FLIP(state); return HORIZONTAL_FLIP(d); }; // DH
		case 7: return DVH_FLIP(state); // DVH
		// case 7: { const State d = DIAGONAL_FLIP(state); const State dv = VERTCAL_FLIP(d); return HORIZONTAL_FLIP(dv); }; // DVH
	}

	return state;
}

std::pair<State, int> get_canonical_state(const State state)
{
	int canonical_index = 0;
	State canonical_state = state;
	for (int i = 1; i < 8; i++)
	{
		const State symmetric_state = get_symmetric_state(state, i);
		if (symmetric_state < canonical_state)
		{
			canonical_index = i;
			canonical_state = symmetric_state;
		}
	}
	return { canonical_state, canonical_index };
}


void insert_final_state(const State new_state, const std::array<Count, 8> & counts)
{
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		if (counts[symmetry] == 0)
			continue;
			
		const State symmetric_state = get_symmetric_state(new_state, symmetry);
		const int hash = state_hash(symmetric_state);
		final_sum += hash * counts[symmetry];
	}
}

void insert_new_state_to_process(const State new_state, const std::array<Count, 8> & counts)
{
	if (current_depth == max_depth - 1)
	{
		// log += " fd-" + std::to_string(state_hash(new_state));
		insert_final_state(new_state, counts);
		return;
	}

	// log += " " + std::to_string(state_hash(new_state));
	auto [it, is_inserted] = new_states_to_process.insert({new_state, counts});
	if (!is_inserted)
	{
		for (int symmetry = 0; symmetry < 8; symmetry++)
		{	
			it->second[symmetry] += counts[symmetry];
		}
	}
	else
	{
		// log += "*";
	}

}

void insert_possible_move(const State new_state, const std::array<Count, 8> & counts)
{
	auto [canonical_state, canonical_index] = get_canonical_state(new_state);

	std::array<Count, 8> new_counts;
	for (int symmetry = 0; symmetry < 8; symmetry++)
	{
		new_counts[symmetry] = counts[symmetric_mult[canonical_index * 8 + symmetry]];
	}

	if ((canonical_state & 0x7)
		&& (canonical_state & (0x7 << 3))
		&& (canonical_state & (0x7 << 6))
		&& (canonical_state & (0x7 << 9))
		&& (canonical_state & (0x7 << 12))
		&& (canonical_state & (0x7 << 15))
		&& (canonical_state & (0x7 << 18))
		&& (canonical_state & (0x7 << 21))
		&& (canonical_state & (0x7 << 24)))
	{
		// log += " ff-" + std::to_string(state_hash(canonical_state));
		insert_final_state(canonical_state, new_counts);
		return;
	}
	insert_new_state_to_process(canonical_state, new_counts);
}

State get_neighbor_mask(const State state, const int position)
{
	const State mask = state & neighbors_mask[position];
	return	(!!(mask & 0b111)) |
			((!!((mask >> 3) & 0b111)) << 1) |
			((!!((mask >> 6) & 0b111)) << 2) |
			((!!((mask >> 9) & 0b111)) << 3) |
			((!!((mask >> 12) & 0b111)) << 4) |
			((!!((mask >> 15) & 0b111)) << 5) |
			((!!((mask >> 18) & 0b111)) << 6) |
			((!!((mask >> 21) & 0b111)) << 7) |
			((!!((mask >> 24) & 0b111)) << 8);
}

void get_possible_moves(const State state, const std::array<Count, 8> & counts)
{
	for (int i = 0; i < 9; i++)
	{
		if (!IS_POSITION_EMPTY(state, i))
			continue;

		const State neighbor_mask = get_neighbor_mask(state, i);
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
	// std::cin >> max_depth; std::cin.ignore();
	
	// State initial_state = 0;
	// for (int i = 0; i < 3; i++)
	// {
	// 	for (int j = 0; j < 3; j++)
	// 	{
	// 		State value;
	// 		std::cin >> value; std::cin.ignore();
	// 		if (value == 0)
	// 			continue;
	// 		initial_state = set_die(initial_state, i * 3 + j, value);
	// 	}
	// }

	max_depth = 20;
	State initial_state = create_state("000000000");
	
	new_states_to_process.reserve(100000);
	states_to_process.reserve(100000);
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
			// log = std::to_string(state_hash(state));
			for (int i = 0; i < 8; i++)
			{
				// log += " " + std::to_string(counts[i]);
			}
			// log += ":";

			get_possible_moves(state, counts);

			// std::cout << log << std::endl;
			iteration++;
		}
		std::swap(states_to_process, new_states_to_process);

		// std::cout << std::endl;
	}
	// std::cout << "Iterations: " << iteration << std::endl;

	const int final_sum = compute_final_sum();
	std::cout << final_sum << std::endl;
}
