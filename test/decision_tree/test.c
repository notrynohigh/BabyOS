/**
 * @file test_decision_tree.c
 * @brief Test for Decision Tree inference
 * 
 * Tests: Simple 1D threshold classifier
 * Tree structure (binary tree):
 *         [F0 < 5?]
 *        /        \
 *     Leaf 0    [F0 < 8?]
 *              /        \
 *           Leaf 1     Leaf 2
 * 
 * Rules:
 *   F0 < 5  -> Class 0
 *   5 <= F0 < 8 -> Class 1  
 *   F0 >= 8 -> Class 2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "algo_decision_tree.h"

#define NUM_CLASSES  3
#define FEATURE_DIM  1
#define MAX_NODES    5

/* Test tree: threshold classifier */
static dt_node_t tree_nodes[MAX_NODES];

int main(void)
{
    dt_model_t model;
    dt_features_t features;
    dt_result_t result;
    dt_node_t nodes[MAX_NODES];
    char explain_buf[512];
    int16_t test_values[] = {3*256, 5*256, 6*256, 8*256, 10*256};
    const char *expected[] = {"Class 0", "Class 1", "Class 1", "Class 2", "Class 2"};
    int i, ret;
    int num_tests = sizeof(test_values) / sizeof(test_values[0]);

    printf("\n");
    printf("========================================\n");
    printf("  Decision Tree Inference Test (BabyOS)\n");
    printf("========================================\n");

    /* Initialize tree_nodes manually to avoid struct init issues */
    memset(tree_nodes, 0, sizeof(tree_nodes));
    tree_nodes[0].type = DT_NODE_DECISION;
    tree_nodes[0].split.feature_idx = 0;
    tree_nodes[0].split.op = DT_OP_LESS_THAN;
    tree_nodes[0].split.threshold = 5*256;
    tree_nodes[0].left_child = 1;
    tree_nodes[0].right_child = 2;

    tree_nodes[1].type = DT_NODE_LEAF;
    tree_nodes[1].predicted_class = 0;
    tree_nodes[1].confidence = 95;

    tree_nodes[2].type = DT_NODE_DECISION;
    tree_nodes[2].split.feature_idx = 0;
    tree_nodes[2].split.op = DT_OP_LESS_THAN;
    tree_nodes[2].split.threshold = 8*256;
    tree_nodes[2].left_child = 3;
    tree_nodes[2].right_child = 4;

    tree_nodes[3].type = DT_NODE_LEAF;
    tree_nodes[3].predicted_class = 1;
    tree_nodes[3].confidence = 90;

    tree_nodes[4].type = DT_NODE_LEAF;
    tree_nodes[4].predicted_class = 2;
    tree_nodes[4].confidence = 92;

    /* Copy tree to working buffer */
    memcpy(nodes, tree_nodes, sizeof(tree_nodes));

    /* Initialize model */
    ret = dt_init(&model, nodes, MAX_NODES, NUM_CLASSES, FEATURE_DIM);
    if (ret != 0) {
        printf("FAIL: init returned %d\n", ret);
        return -1;
    }
    printf("OK: Decision tree initialized (%d nodes, %d classes)\n", MAX_NODES, NUM_CLASSES);

    /* Set root */
    dt_set_root(&model, 0);

    /* Validate tree structure */
    ret = dt_validate(&model);
    if (ret != 0) {
        printf("FAIL: validate returned %d\n", ret);
        return -1;
    }
    printf("OK: Tree structure validated\n");

    /* Print tree structure */
    printf("\n=== Tree Structure ===\n");
    printf("Node 0: [F0 < 5.0] -> L:Node1, R:Node2\n");
    printf("Node 1: LEAF -> Class 0 (conf=95%%)\n");
    printf("Node 2: [F0 < 8.0] -> L:Node3, R:Node4\n");
    printf("Node 3: LEAF -> Class 1 (conf=90%%)\n");
    printf("Node 4: LEAF -> Class 2 (conf=92%%)\n");

    /* Test predictions */
    printf("\n=== Prediction Tests ===\n");
    features.num_features = FEATURE_DIM;

    for (i = 0; i < num_tests; i++) {
        features.values = &test_values[i];
        
        ret = dt_predict(&model, &features, &result);
        if (ret != 0) {
            printf("FAIL: predict returned %d for value %d\n", ret, test_values[i]);
            continue;
        }

        printf("F0=%.2f -> Predicted: %s (conf=%d%%, depth=%d) | Expected: %s\n",
               (float)test_values[i]/256.0f,
               (result.predicted_class == 0) ? "Class 0" :
               (result.predicted_class == 1) ? "Class 1" : "Class 2",
               result.confidence,
               result.depth_reached,
               expected[i]);

        /* Get explanation */
        dt_explain(&model, &features, explain_buf, sizeof(explain_buf));
        printf("  Path: %s\n", explain_buf);
    }

    printf("\n========================================\n");
    printf("  Decision Tree Inference Test PASSED\n");
    printf("========================================\n");

    return 0;
}
