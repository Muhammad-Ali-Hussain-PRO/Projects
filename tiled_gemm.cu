#include <iostream>
#include <cuda_runtime.h>
#include <cmath>

// TILE_DIM defines the width/height of our thread blocks and memory tiles.
// 16x16 threads = 256 threads per block. This maximizes warp efficiency.
#define TILE_DIM 16

/**
 * Optimized Tiled Matrix Multiplication Kernel.
 * Computes C = A * B
 * Matrix sizes: A is (M x K), B is (K x N), C is (M x N)
 */
__global__ void tiledMatrixMul(const float* A, const float* B, float* C, int M, int K, int N) {
    // Statically allocate fast on-chip shared memory for the current tiles
    __shared__ float tileA[TILE_DIM][TILE_DIM];
    __shared__ float tileB[TILE_DIM][TILE_DIM];

    // Identify row and column indices for the output matrix element C
    int row = blockIdx.y * TILE_DIM + threadIdx.y;
    int col = blockIdx.x * TILE_DIM + threadIdx.x;

    float acc = 0.0f; // Accumulator for the inner product dot-product

    // Loop over all the sub-matrix tiles required to compute the dot product
    for (int phase = 0; phase < (K + TILE_DIM - 1) / TILE_DIM; ++phase) {
        
        // 1. Cooperative Load: Thread collaboratively loads one element into tileA
        if (row < M && (phase * TILE_DIM + threadIdx.x) < K) {
            tileA[threadIdx.y][threadIdx.x] = A[row * K + (phase * TILE_DIM + threadIdx.x)];
        } else {
            tileA[threadIdx.y][threadIdx.x] = 0.0f; // Padding out-of-bounds with zero
        }

        // 2. Cooperative Load: Thread collaboratively loads one element into tileB
        if (col < N && (phase * TILE_DIM + threadIdx.y) < K) {
            tileB[threadIdx.y][threadIdx.x] = B[(phase * TILE_DIM + threadIdx.y) * N + col];
        } else {
            tileB[threadIdx.y][threadIdx.x] = 0.0f; // Padding out-of-bounds with zero
        }

        // Synchronize threads in the block to ensure the entire tile is safely loaded
        __syncthreads();

        // 3. Compute: Multiply the two memory tiles currently sitting in fast L1 cache
        for (int i = 0; i < TILE_DIM; ++i) {
            acc += tileA[threadIdx.y][i] * tileB[i][threadIdx.x];
        }

        // Synchronize again to ensure no threads overwrite tiles before others finish computing
        __syncthreads();
    }

    // Write final accumulated dot product back out to slow Global GPU Memory
    if (row < M && col < N) {
        C[row * N + col] = acc;
    }
}

// Host Helper function to verify GPU results using a naive CPU calculation
bool verifyResults(const float* A, const float* B, const float* C, int M, int K, int N) {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            float sum = 0.0f;
            for (int h = 0; h < K; ++h) {
                sum += A[i * K + h] * B[h * N + j];
            }
            if (std::abs(C[i * N + j] - sum) > 1e-3f) {
                std::cerr << "Mismatch at index (" << i << "," << j << ") GPU: " << C[i * N + j] << " CPU: " << sum << std::endl;
                return false;
            }
        }
    }
    return true;
}

int main() {
    // Define problem dimension matrices (Change these to scale the workload)
    const int M = 512;
    const int K = 512;
    const int N = 512;

    size_t sizeA = M * K * sizeof(float);
    size_t sizeB = K * N * sizeof(float);
    size_t sizeC = M * N * sizeof(float);

    // Allocate standard Host (CPU) memory
    float *h_A = (float*)malloc(sizeA);
    float *h_B = (float*)malloc(sizeB);
    float *h_C = (float*)malloc(sizeC);

    // Initialize Host input matrices with random mock floating numbers
    for (int i = 0; i < M * K; ++i) h_A[i] = static_cast<float>(rand()) / RAND_MAX;
    for (int i = 0; i < K * N; ++i) h_B[i] = static_cast<float>(rand()) / RAND_MAX;

    // Allocate Device (GPU VRAM) memory
    float *d_A, *d_B, *d_C;
    cudaMalloc(&d_A, sizeA);
    cudaMalloc(&d_B, sizeB);
    cudaMalloc(&d_C, sizeC);

    // Synchronously copy input data from Host RAM -> Device VRAM
    cudaMemcpy(d_A, h_A, sizeA, cudaMemcpyHostToDevice);
    cudaMemcpy(d_B, h_B, sizeB, cudaMemcpyHostToDevice);

    // Configure Execution Grid Configurations
    // Block Layout: 16x16 spatial structure 
    dim3 dimBlock(TILE_DIM, TILE_DIM);
    // Grid Layout: Calculate required blocks to handle full dimensions
    dim3 dimGrid((N + TILE_DIM - 1) / TILE_DIM, (M + TILE_DIM - 1) / TILE_DIM);

    // Set up CUDA Performance Timing Events
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    std::cout << "Launching Tiled Matrix Multiplication Kernel on GPU..." << std::endl;
    cudaEventRecord(start);

    // Launch the Custom Kernel Execution onto GPU
    tiledMatrixMul<<<dimGrid, dimBlock>>>(d_A, d_B, d_C, M, K, N);

    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float milliseconds = 0;
    cudaEventElapsedTime(&milliseconds, start, stop);
    std::cout << "Kernel execution completed successfully in: " << milliseconds << " ms" << std::endl;

    // Retrieve outputs by copying back Device VRAM -> Host RAM
    cudaMemcpy(h_C, d_C, sizeC, cudaMemcpyDeviceToHost);

    // Validate the correctness against sequential CPU loop
    std::cout << "Validating results with CPU calculation..." << std::endl;
    if (verifyResults(h_A, h_B, h_C, M, K, N)) {
        std::cout << "✅ SUCCESS: GPU results exactly match CPU calculations!" << std::endl;
    } else {
        std::cout << "❌ ERROR: Miscalculated values detected." << std::endl;
    }

    // Clean up allocated system resources
    cudaFree(d_A); cudaFree(d_B); cudaFree(d_C);
    free(h_A); free(h_B); free(h_C);
    cudaEventDestroy(start); cudaEventDestroy(stop);

    return 0;
}
