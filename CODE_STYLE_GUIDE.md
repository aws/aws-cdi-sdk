# Style Guide

This guide is an attempt to quickly answer some basic code formatting questions that may occur. The goal of is that code should match formatting and style of existing code. This guide is only attempting to answer a few of the most common style questions.

---

## C Version

Code should target C99. Do not use non-standard extensions such as GCC's \_\_attribute\_\_ or intrinsic functions.

## Namespaces

Since AWS CDI SDK is C-based, not C++, it does not have the benefit of using formal namespaces. Instead, every exported function, type, and global variable must be prefixed with "cdi". This is intended to minimize the likelihood of symbols colliding with those of user programs that will be built with the SDK. Explicitly, this means:

- types used internally in the C files do not need to be prefixed
- all types (typedef, struct, union) defined in cdi_*_api.h shall be prefixed by "Cdi"
- all functions declared in cdi_*_api.h shall be prefixed by "Cdi"
- all functions exported (i.e., not declared "static") from any C files compiled as part of the SDK shall be prefixed
- all global variables (i.e., not declared "static") from any C files compiled as part of the SDK shall be prefixed

In practice, it is preferred that all symbols have a prefix, as this is the simpler rule to follow.

## Line length

Each line of text in your code should be at most 120 characters long. A line may exceed 120 characters if it is a comment line which is not feasible to split without harming readability, ease of cut and paste or auto-linking.

## Spaces vs. tabs

Use only spaces, and indent 4 spaces at a time. We use spaces for indentation. You should set your editor to emit spaces when you hit the tab key.

## Non-ASCII characters

Non-ASCII characters should not occur in the AWS CDI SDK code base.

## Pointer variable names

The names of pointer variables for character types (strings) must end with "_str". Arrays should end with "_array". All other pointer variable types must end with "_ptr".

For example:

```C
const char* pool_name_str; // All others strings must end with "_ptr"

int* pool_array; // Ok for arrays

int* pool_ptr;   // All other non-char types must end with "_ptr"
```

## Doxygen Comments

AWS CDI SDK uses Doxygen style comments as the source of customer facing API documentation. The build scripts read the source files and create neatly formatted HTML files with hyperlinks tying everything together. Since this is our only means of providing formal documentation of the API to our partners who will be integrating the SDK into their products, it should be clear, complete, and professional.

The Javadoc style is used for most comments but the /// style can be used for Doxygen comments that span only a few lines.

Note that Doxygen comments shouldn't be used for every comment in the code, only for describing interfaces. Mainly this is used in header files. Documenting all of the other functions, structures, enums, etc. is helpful for AWS CDI SDK development and for those applications developers who wish to dig deeper into the SDK for their own purposes. Implementation comments, such as for how an algorithm works, should be done with regular comments unless such details are important to an interface. Where Doxygen comments are appropriate, they double as code comments in addition to being the source of external documentation.
