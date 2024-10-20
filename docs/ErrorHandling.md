# Error Handling

CCC uses a custom type template for error handling called `Result` which
packages together a return type and a pointer to error information. This allows
errors to be treated as values, and enables CCC to be compiled in environments
where C++ exceptions are disabled.

It is defined like so:

```
class [[nodiscard]] Result {
	...
protected:
	Value m_value;
	std::unique_ptr<Error> m_error;
	...
};
```

Note the `nodiscard` attribute. This means if you ignore the return value of a
function returning a `Result` object, the compiler will warn you. Additionally,
if you assign the return value to an object, but do not use the object, your
compiler is likely to give you a warning about an unused variable. These two
warnings in combination make these `Result` types much easier to use.

## Basic Usage

As an example, lets say you wrote a "safe divide" function that returns an error
if you try to divide by zero:

```
Result<f32> safe_divide(f32 x, f32 y)
{
	CCC_CHECK(y != 0, "Division by zero!");
	return x / y;
}
```

Here, the `CCC_CHECK` macro will return a `Result` object containing the error
message `Division by zero!` if `y` is equal to zero.

Now lets say you wanted to call this function from within another:

```
Result<f32> force_due_to_gravity(f32 m1, f32 m2, f32 r)
{
	Result<f32> frac = safe_divide(m1 * m2, r * r);
	CCC_RETURN_IF_ERROR(frac);
	
	return BIG_G * *frac;
}
```

The `CCC_RETURN_IF_ERROR` macro lets you propagate errors up the call stack,
even for `Result` types containing different types of values.

Note that to access the value stored in a `Result` object you can use the
dereference operator (`*`), or the arrow operator (`->`). However, if you try
to do this on a `Result` object that does not contain a value, this will trigger
an assertion failure.

## Macros

CCC provides various error handling macros. They are listed below:

### User-Facing Fatal Errors

These should be used to indicate a fatal error that was caused by the user. For
example, an incorrect command line argument being passed. These should not be
used in the CCC library (the code in `src/ccc/`) itself.

#### `CCC_EXIT`
Print an error message and exit.

#### `CCC_EXIT_IF_FALSE`
Print an error message and exit, but only if the condition passed as the first
argument to the macro evaluates to false.

#### `CCC_EXIT_IF_ERROR`
Print an error message and exit, but only if the provided `Result` object
contains an error.

### Internal Fatal Errors

These should be used for internal logic bugs only. These should not be used for
problems that could be caused by corrupted input data (e.g. symbol tables).

#### `CCC_ABORT_IF_FALSE`
Print an error message and abort, but only if the condition passed as the first
argument to the macro evaluates to false.

#### `CCC_ASSERT`
Print the condition passed as the first argument to the macro and abort, but
only if said condition evaluates to false.

### Recoverable Errors

These should be used for handling corrupted input data, and should be favoured
for errors that occur in the CCC library (the code in `src/ccc/`) itself.

#### `CCC_FAILURE`
Generate a `Result` object containing the provided error message.

#### `CCC_CHECK`
Return a `Result` object containing the provided error message, but only if the
condition passed as the first argument to the macro evaluates to false.

#### `CCC_RETURN_IF_ERROR`
Return from the current function if the provided `Result` object contains an
error. This is used to propagate error information up the call stack.

### Testing

#### `CCC_GTEST_FAIL_IF_ERROR`
Cause the current unit test to fail if the provided `Result` object contains an
error.
