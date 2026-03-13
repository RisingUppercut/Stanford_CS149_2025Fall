#include <stdio.h>
#include <algorithm>
#include <pthread.h>
#include <math.h>

#include "CycleTimer.h"
#include "sqrt_ispc.h"
#include <immintrin.h>

using namespace ispc;

extern void sqrtSerial(int N, float startGuess, float* values, float* output);

static void verifyResult(int N, float* result, float* gold) {
    for (int i=0; i<N; i++) {
        if (fabs(result[i] - gold[i]) > 1e-4) {
            printf("Error: [%d] Got %f expected %f\n", i, result[i], gold[i]);
        }
    }
}


#include <immintrin.h>

void sqrt_avx2(int N, float startGuess, float* values, float* output) {
    __m256 v_half = _mm256_set1_ps(0.5f);
    __m256 v_three = _mm256_set1_ps(3.0f);
    __m256 v_one = _mm256_set1_ps(1.0f);
    __m256 v_threshold = _mm256_set1_ps(0.00001f);
    __m256 abs_mask = _mm256_set1_ps(-0.0f);

    for (int i = 0; i < N; i += 8) {
        __m256 x = _mm256_set1_ps(startGuess); 
        __m256 v_val = _mm256_loadu_ps(&values[i]);
        
        while (true) {
            // error = fabs(x * x * v_val - 1.0f)
            __m256 x2 = _mm256_mul_ps(x, x);
            __m256 x2v = _mm256_mul_ps(x2, v_val);
            __m256 diff = _mm256_sub_ps(x2v, v_one);
            __m256 error = _mm256_andnot_ps(abs_mask, diff);
            
            __m256 mask = _mm256_cmp_ps(error, v_threshold, _CMP_GT_OQ);
            if (_mm256_movemask_ps(mask) == 0) break;
            
            // guess = (3.f * guess - x * guess * guess * guess) * 0.5f;
            // x_next = 0.5 * (3 * x - v_val * x * x * x)
            __m256 x3 = _mm256_mul_ps(x2, x);
            __m256 term2 = _mm256_mul_ps(v_val, x3);
            __m256 term1 = _mm256_mul_ps(v_three, x);
            __m256 next_x = _mm256_mul_ps(_mm256_sub_ps(term1, term2), v_half);
          
            x = _mm256_blendv_ps(x, next_x, mask);
        }
        
        __m256 final_res = _mm256_mul_ps(v_val, x);
        _mm256_storeu_ps(&output[i], final_res);
    }
}

int main() {

    const unsigned int N = 20 * 1000 * 1000;
    const float initialGuess = 1.0f;

    float* values = new float[N];
    float* output = new float[N];
    float* gold = new float[N];

    for (unsigned int i=0; i<N; i++)
    {
        // TODO: CS149 students.  Attempt to change the values in the
        // array here to meet the instructions in the handout: we want
        // to you generate best and worse-case speedups
        
        // starter code populates array with random input values
        values[i] = .001f + 2.998f * static_cast<float>(rand()) / RAND_MAX;

        // maximum
        // values[i] = 2.999999;

        // mininum
        // values[i] = 1;
        // if (i % 8 == 0) {
        //     values[i] = 2.999999;
        // }
    }

    // generate a gold version to check results
    for (unsigned int i=0; i<N; i++)
        gold[i] = sqrt(values[i]);

    //
    // And run the serial implementation 3 times, again reporting the
    // minimum time.
    //
    double minSerial = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrtSerial(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minSerial = std::min(minSerial, endTime - startTime);
    }

    printf("[sqrt serial]:\t\t[%.3f] ms\n", minSerial * 1000);

    verifyResult(N, output, gold);

    //
    // Compute the image using the ispc implementation; report the minimum
    // time of three runs.
    //
    double minISPC = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrt_ispc(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minISPC = std::min(minISPC, endTime - startTime);
    }

    printf("[sqrt ispc]:\t\t[%.3f] ms\n", minISPC * 1000);

    verifyResult(N, output, gold);

    // Clear out the buffer
    for (unsigned int i = 0; i < N; ++i)
        output[i] = 0;

    //
    // Tasking version of the ISPC code
    //
    double minTaskISPC = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrt_ispc_withtasks(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minTaskISPC = std::min(minTaskISPC, endTime - startTime);
    }

    printf("[sqrt task ispc]:\t[%.3f] ms\n", minTaskISPC * 1000);

    verifyResult(N, output, gold);

    // Clear out the buffer
    for (unsigned int i = 0; i < N; ++i)
        output[i] = 0;
    
    //
    // AVX2
    //
    double minAvx = 1e30;
    for (int i = 0; i < 3; ++i) {
        double startTime = CycleTimer::currentSeconds();
        sqrt_avx2(N, initialGuess, values, output);
        double endTime = CycleTimer::currentSeconds();
        minAvx = std::min(minAvx, endTime - startTime);
    }

    printf("[avx2]:\t[%.3f] ms\n", minAvx * 1000);

    verifyResult(N, output, gold);
    
    printf("\t\t\t\t(%.2fx speedup from ISPC)\n", minSerial/minISPC);
    printf("\t\t\t\t(%.2fx speedup from task ISPC)\n", minSerial/minTaskISPC);
    printf("\t\t\t\t(%.2fx speedup from avx)\n", minSerial/minAvx);

    delete [] values;
    delete [] output;
    delete [] gold;

    return 0;
}
