// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

// ee-g++ operators.cpp -o operators.elf -nostdlib -gstabs+

typedef unsigned int size_t;

struct Operators {
	// Constructors
	void constructor() {}
	Operators() {}
	void copy_constructor() {}
	Operators(const Operators&) {}
	
	// Destructor
	void destructor() {}
	~Operators() {}
	
	// Arithmetic operators
	void addition() {}
	Operators operator+(Operators) { return *this; }
	void subtraction() {}
	Operators operator-(Operators) { return *this; }
	void unary_plus() {}
	Operators operator+() { return *this; }
	void unary_minus() {}
	Operators operator-() { return *this; }
	void multiplication() {}
	Operators operator*(Operators) { return *this; }
	void division() {}
	Operators operator/(Operators) { return *this; }
	void modulo() {}
	Operators operator%(Operators) { return *this; }
	void prefix_increment() {}
	Operators operator++() { return *this; }
	void postfix_increment() {}
	Operators operator++(int) { return *this; }
	void prefix_decrement() {}
	Operators operator--() { return *this; }
	void postfix_decrement() {}
	Operators operator--(int) { return *this; }
	
	// Comparison operators
	void equal() {}
	bool operator==(const Operators&) { return false; }
	void not_equal() {}
	bool operator!=(const Operators&) { return false; }
	void greater_than() {}
	bool operator>(const Operators&) { return false; }
	void less_than() {}
	bool operator<(const Operators&) { return false; }
	void greater_than_or_equal() {}
	bool operator>=(const Operators&) { return false; }
	void less_than_or_equal() {}
	bool operator<=(const Operators&) { return false; }
	
	// Logical operators
	void logical_negation() {}
	bool operator!() { return false; }
	void logical_and() {}
	bool operator&&(Operators&) { return false; }
	void logical_or() {}
	bool operator||(Operators) { return false; }
	
	// Bitwise operators
	void bitwise_not() {}
	Operators operator~() { return *this; }
	void bitwise_and() {}
	Operators operator&(Operators) { return *this; }
	void bitwise_or() {}
	Operators operator|(Operators) { return *this; }
	void bitwise_xor() {}
	Operators operator^(Operators) { return *this; }
	void bitwise_left_shift() {}
	Operators operator<<(Operators) { return *this; }
	void bitwise_right_shift() {}
	Operators operator>>(Operators) { return *this; }
	
	// Assignment operators
	void direct_assignment() {}
	Operators& operator=(int) { return *this; }
	void addition_assigment() {}
	Operators& operator+=(int) { return *this; }
	void subtraction_assignment() {}
	Operators& operator-=(int) { return *this; }
	void multiplication_assignment() {}
	Operators& operator*=(int) { return *this; }
	void division_assignment() {}
	Operators& operator/=(int) { return *this; }
	void modulo_assignment() {}
	Operators& operator%=(int) { return *this; }
	void bitwise_and_assignment() {}
	Operators& operator&=(int) { return *this; }
	void bitwise_or_assignment() {}
	Operators& operator|=(int) { return *this; }
	void bitwise_xor_assignment() {}
	Operators& operator^=(int) { return *this; }
	void bitwise_left_shift_assignment() {}
	Operators& operator<<(int) { return *this; }
	void bitwise_right_shift_assignment() {}
	Operators& operator>>(int) { return *this; }
	
	// Member and pointer operators
	void subscript() {}
	Operators& operator[](int) { return *this; }
	void indirection() {}
	Operators& operator*() { return *this; }
	void address_of() {}
	Operators* operator&() { return this; }
	void structure_dereference() {}
	Operators* operator->() { return this; }
	void member_selected_by_pointer_to_member() {}
	Operators& operator->*(int) { return *this; }
	
	// Other operators
	void function_call() {}
	void operator()() {}
	void comma() {}
	void operator,(int) {}
	void conversion() {}
	operator int() { return 0; }
	void allocate_storage() {}
	void* operator new(size_t) { return (void*) 0; }
	void allocate_storage_array() {}
	void* operator new[](size_t) { return (void*) 0; }
	void deallocate_storage();
	void operator delete(void*) {}
	void deallocate_storage_array() {}
	void operator delete[](void*) {}
};
