#include <stdio.h>
#include <time.h>

int main() {
    printf("CPU workload started...\n");
    clock_t start = clock();
    
    volatile double result = 0.0;
    // A massive loop to burn CPU cycles
    for (long long i = 0; i < 500000000LL; i++) {
        result += 1.0;
    }
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("CPU workload finished in %f seconds!\n", time_spent);
    return 0;
}
