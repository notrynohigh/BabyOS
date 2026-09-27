/**
 * \file        test_algo_ml.c
 * \brief       ML basic primitives (algo_ml) tests for BabyOS selftest
 * \author      BabyOS Team
 *
 * Tests: argmax (incl. tie min-index), softmax (numerical stability +
 * normalization), dot (f32 accumulation order), normalize, relu,
 * sigmoid (monotonic + +-extremes), tree predict (manual walk + depth
 * boundary). Contract: tool/automl/docs/requirements.md FR-10.
 *******************************************************************************
 */

#include "../port.h"
#include "b_config.h"
#include "b_os.h"
#include "algorithm/inc/algo_ml.h"

#if (defined(_UNITY_ENABLE) && (_UNITY_ENABLE == 1))

/*------------------------------------------------------------
 * Argmax tests — strict '>' scan, tie -> smallest index (sklearn aligned)
 *------------------------------------------------------------*/
void test_bAlgoMlArgmax(void)
{
    float v[] = {0.1f, 0.9f, 0.4f};
    TEST_ASSERT_EQUAL_INT(1, bAlgoMlArgmax(v, 3));
}

void test_bAlgoMlArgmaxTie(void)
{
    /* exact tie -> smallest index */
    float v[] = {0.5f, 0.9f, 0.9f};
    TEST_ASSERT_EQUAL_INT(1, bAlgoMlArgmax(v, 3));
    float w[] = {0.9f, 0.9f, 0.9f};
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlArgmax(w, 3));
}

void test_bAlgoMlArgmaxSingle(void)
{
    float v[] = {-3.0f};
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlArgmax(v, 1));
}

/*------------------------------------------------------------
 * Softmax tests — subtract max first (stability), sums to 1
 *------------------------------------------------------------*/
void test_bAlgoMlSoftmax(void)
{
    float src[] = {1.0f, 2.0f, 3.0f};
    float dst[3];
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlSoftmax(dst, src, 3));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.09003057f, dst[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.24472847f, dst[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.66524096f, dst[2]);
    /* src untouched (non-inplace path) */
    TEST_ASSERT_EQUAL_FLOAT(2.0f, src[1]);
}

void test_bAlgoMlSoftmaxStable(void)
{
    /* large offsets: subtract-max keeps exp in range; result identical
     * to the shifted {1,2,3} case (shift invariance) */
    float src[] = {1000.0f, 1001.0f, 1002.0f};
    float dst[3];
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlSoftmax(dst, src, 3));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.09003057f, dst[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.24472847f, dst[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.66524096f, dst[2]);
}

/*------------------------------------------------------------
 * Dot test — f32 sequential accumulation, hand-computed reference
 *------------------------------------------------------------*/
void test_bAlgoMlDot(void)
{
    float a[] = {1.5f, -2.0f, 3.25f};
    float b[] = {0.5f, 4.0f, -1.0f};
    /* acc = 0 + 0.75; acc = 0.75 - 8.0; acc = -7.25 - 3.25 = -10.5 */
    TEST_ASSERT_EQUAL_FLOAT(-10.5f, bAlgoMlDot(a, b, 3));
}

/*------------------------------------------------------------
 * Normalize test — dst[i] = (src[i]-offset[i]) * inv_scale[i]
 *------------------------------------------------------------*/
void test_bAlgoMlNormalize(void)
{
    float src[] = {10.0f, 20.0f};
    float offset[] = {5.0f, 15.0f};
    float inv[] = {0.5f, 0.25f};
    float dst[2];
    bAlgoMlNormalize(dst, src, offset, inv, 2);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, dst[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.25f, dst[1]);
}

/*------------------------------------------------------------
 * Relu tests
 *------------------------------------------------------------*/
void test_bAlgoMlRelu(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bAlgoMlRelu(-3.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bAlgoMlRelu(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, bAlgoMlRelu(2.5f));
}

/*------------------------------------------------------------
 * Sigmoid tests — no clipping: expf overflow -> inf -> 0 naturally
 *------------------------------------------------------------*/
void test_bAlgoMlSigmoid(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.5f, bAlgoMlSigmoid(0.0f));
    /* monotonic */
    TEST_ASSERT_TRUE(bAlgoMlSigmoid(1.0f) > bAlgoMlSigmoid(0.0f));
    TEST_ASSERT_TRUE(bAlgoMlSigmoid(0.0f) > bAlgoMlSigmoid(-1.0f));
    /* +extreme: expf(-100) underflows to 0 -> exactly 1 */
    TEST_ASSERT_EQUAL_FLOAT(1.0f, bAlgoMlSigmoid(100.0f));
    /* -extreme: expf(+100) overflows to inf -> 1/(1+inf) = 0 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bAlgoMlSigmoid(-100.0f));
}

/*------------------------------------------------------------
 * TreePredict tests — manual tree walk + proba_off + depth boundary
 *
 *      node0 (f0 <= 1.5)
 *       /            \
 *   node1 (leaf,    node2 (leaf,
 *   proba_off=0)    proba_off=1)
 *------------------------------------------------------------*/
void test_bAlgoMlTreePredict(void)
{
    static const bAlgoMlTreeNode_t nodes[] = {
        {0, 1, 2, -1, 1.5},
        {-1, -1, -1, 0, 0.0},
        {-1, -1, -1, 1, 0.0},
    };
    float xf_left[] = {1.0f};
    float xf_right[] = {2.0f};
    float xf_edge[] = {1.5f}; /* <= threshold walks left */
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlTreePredict(nodes, 3, xf_left, 1));
    TEST_ASSERT_EQUAL_INT(1, bAlgoMlTreePredict(nodes, 3, xf_right, 1));
    TEST_ASSERT_EQUAL_INT(0, bAlgoMlTreePredict(nodes, 3, xf_edge, 1));
}

void test_bAlgoMlTreePredictArg(void)
{
    static const bAlgoMlTreeNode_t leaf[] = {{-1, -1, -1, 0, 0.0}};
    float xf[] = {0.0f};
    TEST_ASSERT_EQUAL_INT(-1, bAlgoMlTreePredict(NULL, 1, xf, 1));
    TEST_ASSERT_EQUAL_INT(-1, bAlgoMlTreePredict(leaf, 0, xf, 1));
    TEST_ASSERT_EQUAL_INT(-1, bAlgoMlTreePredict(leaf, 1, NULL, 1));
}

void test_bAlgoMlTreePredictDepth(void)
{
    /* Chain of internal nodes each walking left; depth exactly at the
     * contract limit succeeds, one over returns -2 (generator blocks
     * deeper trees at export time; this is the runtime backstop). */
    static bAlgoMlTreeNode_t chain[ALGO_ML_TREE_MAX_DEPTH + 2];
    float                  xf[] = {0.0f};
    uint32_t               i;

    for (i = 0; i < ALGO_ML_TREE_MAX_DEPTH; i++)
    {
        chain[i].feature   = 0;
        chain[i].left      = (int16_t)(i + 1);
        chain[i].right     = (int16_t)(i + 1);
        chain[i].proba_off = -1;
        chain[i].threshold = 1.0; /* 0.0 <= 1.0 always left */
    }
    /* leaf at index ALGO_ML_TREE_MAX_DEPTH -> path depth == limit: OK */
    chain[ALGO_ML_TREE_MAX_DEPTH].feature   = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH].left      = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH].right     = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH].proba_off = 0;
    chain[ALGO_ML_TREE_MAX_DEPTH].threshold = 0.0;
    TEST_ASSERT_EQUAL_INT(0,
                          bAlgoMlTreePredict(chain, ALGO_ML_TREE_MAX_DEPTH + 1, xf, 1));

    /* one more internal node -> depth limit exceeded: -2 */
    chain[ALGO_ML_TREE_MAX_DEPTH].feature   = 0;
    chain[ALGO_ML_TREE_MAX_DEPTH].left      = (int16_t)(ALGO_ML_TREE_MAX_DEPTH + 1);
    chain[ALGO_ML_TREE_MAX_DEPTH].right     = (int16_t)(ALGO_ML_TREE_MAX_DEPTH + 1);
    chain[ALGO_ML_TREE_MAX_DEPTH].proba_off = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH].threshold = 1.0;
    chain[ALGO_ML_TREE_MAX_DEPTH + 1].feature   = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH + 1].left      = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH + 1].right     = -1;
    chain[ALGO_ML_TREE_MAX_DEPTH + 1].proba_off = 0;
    chain[ALGO_ML_TREE_MAX_DEPTH + 1].threshold = 0.0;
    TEST_ASSERT_EQUAL_INT(-2,
                          bAlgoMlTreePredict(chain, ALGO_ML_TREE_MAX_DEPTH + 2, xf, 1));
}

int test_algo_ml_runner(void)
{
    int start = Unity.TestFailures;
    RUN_TEST(test_bAlgoMlArgmax);
    RUN_TEST(test_bAlgoMlArgmaxTie);
    RUN_TEST(test_bAlgoMlArgmaxSingle);
    RUN_TEST(test_bAlgoMlSoftmax);
    RUN_TEST(test_bAlgoMlSoftmaxStable);
    RUN_TEST(test_bAlgoMlDot);
    RUN_TEST(test_bAlgoMlNormalize);
    RUN_TEST(test_bAlgoMlRelu);
    RUN_TEST(test_bAlgoMlSigmoid);
    RUN_TEST(test_bAlgoMlTreePredict);
    RUN_TEST(test_bAlgoMlTreePredictArg);
    RUN_TEST(test_bAlgoMlTreePredictDepth);
    return Unity.TestFailures - start;
}

#endif /* _UNITY_ENABLE */
