#include <iostream>
#include <vector>
#include <chrono>

// Define a simple matrix type
using Matrix = std::vector<std::vector<int>>;

// Matrix multiplication function
Matrix multiply(const Matrix &A, const Matrix &B) {
    size_t n = A.size();
    size_t m = A[0].size();
    size_t p = B[0].size();
    
    Matrix C(n, std::vector<int>(p, 0));
    
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < p; ++j) {
            for (size_t k = 0; k < m; ++k) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
    
    return C;
}

int main() {
    size_t N = 790;  // matrix size approximated to achieve 1b instructions
    Matrix A(N, std::vector<int>(N, 2));  // NxN matrix initialized with all 2s
    Matrix B(N, std::vector<int>(N, 3));  // NxN matrix initialized with all 3s

    auto start = std::chrono::high_resolution_clock::now();
    Matrix C = multiply(A, B);
    auto stop = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "Matrix multiplication took " << duration.count() << " milliseconds." << std::endl;

    return 0;
}
