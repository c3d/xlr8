#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

uint64_t fib(uint64_t n)
{
    return n < 2 ? n : fib(n-1) + fib(n-2);
}

int main(int argc, char **argv)
{
    uint64_t n = argc > 1 ? atoi(argv[1]) : 40;
    printf("Fib(%llu)=%llu\n", n, fib(n));
}
