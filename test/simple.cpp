// ee-g++ simple.cpp -o simple.elf -nostdlib -gstabs

typedef int int128 __attribute__((mode(TI)));

struct Primitives {
	char c;
	short s;
	int i;
	long l;
	long long ll;
	float f;
	_Complex int c1;
	_Complex float c2;
	_Complex double c3;
	int128 qwd;
};

struct PointersAndArrays {
	void *a;
	Primitives ****b;
	int array[123];
	int multiarray[2][4][6];
	void (*func_ptr_array[4])(int, int);
	int zerolength[0];
};

struct Bitfields {
	int one : 8;
	int two : 8;
	int three : 16;
	int four : 16;
};

struct NestedStructs {
	struct NestedA {
		int field1;
	};
	
	struct {
		int field2;
	} nested_b;
	
	struct NestedC {
		int field2;
	} nested_c;
};

typedef struct {
	int a, b, c;
} TypedefedStruct;

struct MemberPointer {
	int TypedefedStruct::*pointertomember;
};

Primitives globalvariable;

void func(int x, int128 y) {
	int a = 9;
	int128 b;
}
