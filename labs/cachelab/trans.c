/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, ii, jj;
    int v0, v1, v2, v3, v4, v5, v6, v7;

    if (M == 32 && N == 32) {
        // 32x32: 使用 8x8 分块
        for (ii = 0; ii < N; ii += 8) {
            for (jj = 0; jj < M; jj += 8) {
                for (i = ii; i < ii + 8; i++) {
                    // 读取一行的 8 个元素到局部变量
                    v0 = A[i][jj];
                    v1 = A[i][jj + 1];
                    v2 = A[i][jj + 2];
                    v3 = A[i][jj + 3];
                    v4 = A[i][jj + 4];
                    v5 = A[i][jj + 5];
                    v6 = A[i][jj + 6];
                    v7 = A[i][jj + 7];

                    // 写入到 B 的对应列
                    B[jj][i] = v0;
                    B[jj + 1][i] = v1;
                    B[jj + 2][i] = v2;
                    B[jj + 3][i] = v3;
                    B[jj + 4][i] = v4;
                    B[jj + 5][i] = v5;
                    B[jj + 6][i] = v6;
                    B[jj + 7][i] = v7;
                }
            }
        }
    } else if (M == 64 && N == 64) {
        // 64x64: 使用 8x8 分块，分成4个4x4子块处理
        for (ii = 0; ii < N; ii += 8) {
            for (jj = 0; jj < M; jj += 8) {
                // 处理上半部分 (4行)
                for (i = ii; i < ii + 4; i++) {
                    // 读取 A 的一行 (8个元素)
                    v0 = A[i][jj];
                    v1 = A[i][jj + 1];
                    v2 = A[i][jj + 2];
                    v3 = A[i][jj + 3];
                    v4 = A[i][jj + 4];
                    v5 = A[i][jj + 5];
                    v6 = A[i][jj + 6];
                    v7 = A[i][jj + 7];

                    // 左上4x4转置到B的左上
                    B[jj][i] = v0;
                    B[jj + 1][i] = v1;
                    B[jj + 2][i] = v2;
                    B[jj + 3][i] = v3;

                    // 右上4x4暂存到B的右上（转置位置）
                    B[jj][i + 4] = v4;
                    B[jj + 1][i + 4] = v5;
                    B[jj + 2][i + 4] = v6;
                    B[jj + 3][i + 4] = v7;
                }

                // 处理下半部分，同时调整右上的暂存数据
                for (j = jj; j < jj + 4; j++) {
                    // 读取B右上的暂存数据
                    v0 = B[j][ii + 4];
                    v1 = B[j][ii + 5];
                    v2 = B[j][ii + 6];
                    v3 = B[j][ii + 7];

                    // 读取A左下的数据
                    v4 = A[ii + 4][j];
                    v5 = A[ii + 5][j];
                    v6 = A[ii + 6][j];
                    v7 = A[ii + 7][j];

                    // 将A左下写入B左下
                    B[j][ii + 4] = v4;
                    B[j][ii + 5] = v5;
                    B[j][ii + 6] = v6;
                    B[j][ii + 7] = v7;

                    // 将暂存数据写入B右上
                    B[j + 4][ii] = v0;
                    B[j + 4][ii + 1] = v1;
                    B[j + 4][ii + 2] = v2;
                    B[j + 4][ii + 3] = v3;
                }

                // 处理右下4x4
                for (i = ii + 4; i < ii + 8; i++) {
                    v0 = A[i][jj + 4];
                    v1 = A[i][jj + 5];
                    v2 = A[i][jj + 6];
                    v3 = A[i][jj + 7];

                    B[jj + 4][i] = v0;
                    B[jj + 5][i] = v1;
                    B[jj + 6][i] = v2;
                    B[jj + 7][i] = v3;
                }
            }
        }
    } else {
        // 61x67 和其他: 使用 16x16 分块
        for (ii = 0; ii < N; ii += 16) {
            for (jj = 0; jj < M; jj += 16) {
                for (i = ii; i < ii + 16 && i < N; i++) {
                    for (j = jj; j < jj + 16 && j < M; j++) {
                        B[j][i] = A[i][j];
                    }
                }
            }
        }
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

