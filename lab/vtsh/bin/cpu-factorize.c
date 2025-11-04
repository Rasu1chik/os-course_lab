#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


int is_prime(long long n) {
  if (n < 2)
    return 0;
  if (n == 2)
    return 1;
  if (n % 2 == 0)
    return 0;

  for (long long i = 3; i * i <= n; i += 2) {
    if (n % i == 0)
      return 0;
  }
  return 1;
}


long long generate_composite_number() {

  long long primes[] = {1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049};
  int num_primes = sizeof(primes) / sizeof(primes[0]);

  
  srand(time(NULL));
  long long result = 1;
  int count = 3 + (rand() % 3);  

  for (int i = 0; i < count; i++) {
    int idx = rand() % num_primes;
    result *= primes[idx];
  }

  return result;
}


void factorize(long long n, long long* factors, int* num_factors) {
  *num_factors = 0;

  // Проверяем делимость на 2
  while (n % 2 == 0) {
    factors[(*num_factors)++] = 2;
    n /= 2;
  }


  for (long long i = 3; i * i <= n; i += 2) {
    while (n % i == 0) {
      factors[(*num_factors)++] = i;
      n /= i;
    }
  }


  if (n > 1) {
    factors[(*num_factors)++] = n;
  }
}


int verify_factorization(
    long long original, long long* factors, int num_factors
) {
  long long product = 1;
  for (int i = 0; i < num_factors; i++) {
    product *= factors[i];
  }
  return product == original;
}

void print_usage(const char* program_name) {
  printf("Usage: %s --number <N> --iterations <I>\n", program_name);
  printf(
      "  --number <N>     : Number to factorize (default: random composite)\n"
  );
  printf("  --iterations <I> : Number of iterations (default: 1)\n");
  printf("  --help           : Show this help\n");
}

int main(int argc, char* argv[]) {
  long long number = 0;
  int iterations = 1;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--number") == 0) {
      if (i + 1 < argc) {
        number = atoll(argv[++i]);
      } else {
        fprintf(stderr, "Error: --number requires a value\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--iterations") == 0) {
      if (i + 1 < argc) {
        iterations = atoi(argv[++i]);
      } else {
        fprintf(stderr, "Error: --iterations requires a value\n");
        return 1;
      }
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }


  if (number == 0) {
    number = generate_composite_number();
    printf("Generated composite number: %lld\n", number);
  }

  printf("Factorizing %lld for %d iterations...\n", number, iterations);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (int iter = 0; iter < iterations; iter++) {
    long long factors[64];
    int num_factors;

   
    factorize(number, factors, &num_factors);

    
    if (!verify_factorization(number, factors, num_factors)) {
      fprintf(stderr, "Error: Factorization verification failed!\n");
      return 1;
    }

    
    if (iter == 0) {
      printf("Prime factors: ");
      for (int i = 0; i < num_factors; i++) {
        printf("%lld", factors[i]);
        if (i < num_factors - 1)
          printf(" * ");
      }
      printf(" = %lld\n", number);
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  
  long seconds = end.tv_sec - start.tv_sec;
  long nanoseconds = end.tv_nsec - start.tv_nsec;
  if (nanoseconds < 0) {
    seconds--;
    nanoseconds += 1000000000;
  }

  printf("Total execution time: %ld.%09lds\n", seconds, nanoseconds);
  printf(
      "Average time per iteration: %ld.%09lds\n",
      seconds / iterations,
      (nanoseconds / iterations) % 1000000000
  );

  return 0;
}
