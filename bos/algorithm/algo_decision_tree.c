/**
 *!
 * \file        algo_decision_tree.c
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Decision Tree Inference implementation
 *******************************************************************************
 */
#include "inc/algo_decision_tree.h"
#include <string.h>
#include <stdio.h>

#if (defined(_ALGO_DECISION_TREE_ENABLE) && (_ALGO_DECISION_TREE_ENABLE == 1))

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup DECISION_TREE
 * \{
 */

/**
 * @brief Check if a split condition is satisfied
 */
static uint8_t dt_check_split(dt_split_t *split, int16_t feature_value)
{
    switch (split->op) {
        case DT_OP_LESS_THAN:
            return (feature_value < split->threshold) ? 1 : 0;
        case DT_OP_LESS_EQUAL:
            return (feature_value <= split->threshold) ? 1 : 0;
        case DT_OP_GREATER_THAN:
            return (feature_value > split->threshold) ? 1 : 0;
        case DT_OP_EQUAL:
            return (feature_value == split->threshold) ? 1 : 0;
    }
    return 0;
}

int dt_init(dt_model_t *model, dt_node_t *nodes, uint16_t num_nodes,
            uint8_t num_classes, uint8_t feature_dim)
{
    if (model == NULL || nodes == NULL) {
        return -1;
    }

    model->nodes = nodes;
    model->num_nodes = num_nodes;
    model->num_classes = num_classes;
    model->feature_dim = feature_dim;
    model->root_idx = -1;

    return 0;
}

void dt_set_root(dt_model_t *model, int16_t root_idx)
{
    if (model) {
        model->root_idx = root_idx;
    }
}

int dt_predict(dt_model_t *model, dt_features_t *features, dt_result_t *result)
{
    int16_t current_idx;
    uint16_t depth = 0;

    if (model == NULL || features == NULL || result == NULL) {
        return -1;
    }

    if (model->root_idx < 0 || model->root_idx >= model->num_nodes) {
        return -2;
    }

    if (features->num_features != model->feature_dim) {
        return -3;
    }

    current_idx = model->root_idx;

    /* Traverse tree */
    while (1) {
        dt_node_t *node;

        if (current_idx < 0 || current_idx >= model->num_nodes) {
            return -5;  /* Invalid node index */
        }

        node = &model->nodes[current_idx];

        if (node->type == DT_NODE_LEAF) {
            result->predicted_class = node->predicted_class;
            result->confidence = node->confidence;
            result->depth_reached = depth;
            return 0;
        }

        /* Decision node: check split condition */
        if (node->split.feature_idx >= model->feature_dim) {
            return -4;  /* Invalid feature index */
        }

        {
            int16_t fval = features->values[node->split.feature_idx];
            if (fval < node->split.threshold) {
                current_idx = node->left_child;
            } else {
                current_idx = node->right_child;
            }
        }

        depth++;
        if (depth > 16) {
            return -6;  /* Exceeded max depth (safeguard) */
        }
    }
}

int dt_explain(dt_model_t *model, dt_features_t *features, char *path_out, uint16_t path_max_len)
{
    int16_t current_idx;
    uint16_t depth = 0;
    int16_t pos;

    if (model == NULL || features == NULL || path_out == NULL) {
        return -1;
    }

    if (model->root_idx < 0) {
        return -2;
    }

    path_out[0] = '\0';
    current_idx = model->root_idx;

    while (depth < model->num_nodes && path_max_len > 20) {
        dt_node_t *node = &model->nodes[current_idx];

        if (node->type == DT_NODE_LEAF) {
            char buf[64];
            pos = strlen(path_out);
            snprintf(&path_out[pos], path_max_len - pos, " -> LEAF(class=%d, conf=%d%%)",
                    node->predicted_class, node->confidence);
            return depth + 1;
        }

        /* Add decision to path */
        char buf[64];
        uint8_t go_left = dt_check_split(&node->split, features->values[node->split.feature_idx]);

        const char *op_str;
        switch (node->split.op) {
            case DT_OP_LESS_THAN:    op_str = "<";  break;
            case DT_OP_LESS_EQUAL:    op_str = "<="; break;
            case DT_OP_GREATER_THAN: op_str = ">";  break;
            case DT_OP_EQUAL:        op_str = "=="; break;
            default:                  op_str = "?";  break;
        }

        pos = strlen(path_out);
        snprintf(&path_out[pos], path_max_len - pos,
                "%sF%d(%d) %s %d%s",
                (depth == 0) ? "" : " -> ",
                node->split.feature_idx,
                features->values[node->split.feature_idx],
                op_str,
                node->split.threshold,
                go_left ? " [LEFT]" : " [RIGHT]");

        /* Traverse */
        current_idx = go_left ? node->left_child : node->right_child;
        if (current_idx < 0 || current_idx >= model->num_nodes) {
            return -3;
        }

        depth++;
    }

    return depth;
}

int dt_validate(dt_model_t *model)
{
    uint16_t i;

    if (model == NULL || model->nodes == NULL) {
        return -1;
    }

    if (model->root_idx < 0 || model->root_idx >= model->num_nodes) {
        return -2;
    }

    /* Check all decision nodes have valid children */
    for (i = 0; i < model->num_nodes; i++) {
        dt_node_t *node = &model->nodes[i];

        if (node->type == DT_NODE_DECISION) {
            if (node->left_child < 0 || node->left_child >= model->num_nodes) {
                return -3;  /* Invalid left child */
            }
            if (node->right_child < 0 || node->right_child >= model->num_nodes) {
                return -4;  /* Invalid right child */
            }
            if (node->split.feature_idx >= model->feature_dim) {
                return -5;  /* Invalid feature index */
            }
        } else if (node->type == DT_NODE_LEAF) {
            if (node->predicted_class < 0 || node->predicted_class >= model->num_classes) {
                return -6;  /* Invalid class */
            }
        }
    }

    return 0;  /* Valid */
}

/**
 * \}
 */

/**
 * \}
 */

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
