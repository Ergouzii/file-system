# References:

`tokenize()`: assignment1 description

`setBit`: lab 9 tutorial

# Design choices:

When program starts, I read the input file and read content line by line. For each line, I use space to split them. The first element split is `cmd`.

Then I compare each `cmd` to the commands asked in assignment description. If `cmd` matches none of the cases, an error message is printed.

After finishing each function, I give them a few lines of input and test them well. After testing a function, I start writing the next one.