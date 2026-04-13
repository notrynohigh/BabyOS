/**
 *!
 * \file        algo_decision_tree.h
 * \version     v0.0.1
 * \date        2026/04/13
 * \author      aiclaw
 *******************************************************************************
 * @brief Decision Tree Inference for embedded classification
 *******************************************************************************
 */
#ifndef __ALGO_DECISION_TREE_H__
#define __ALGO_DECISION_TREE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * \addtogroup ALGORITHM
 * \{
 */

/**
 * \addtogroup DECISION_TREE
 * \{
 */

/**
 * @brief Feature comparison operator
 */
typedef enum {
    DT_OP_LESS_THAN = 0,     /**< feature < threshold */
    DT_OP_LESS_EQUAL,        /**< feature <= threshold */
    DT_OP_GREATER_THAN,      /**< feature > threshold */
    DT_OP_EQUAL,             /**< feature == threshold (for discrete) */
} dt_operator_t;

/**
 * @brief Node type
 */
typedef enum {
    DT_NODE_DECISION = 0,    /**< Split node (has children) */
    DT_NODE_LEAF,             /**< Leaf node (has prediction) */
} dt_node_type_t;

/**
 * @brief Feature split definition
 */
typedef struct {
    uint8_t feature_idx;      /**< Which feature to compare */
    dt_operator_t op;        /**< Comparison operator */
    int16_t threshold;       /**< Threshold (Q8.8 fixed-point) */
} dt_split_t;

/**
 * @brief Decision tree node
 */
typedef struct {
    dt_node_type_t type;     /**< Node type */
    dt_split_t split;         /**< Split condition (if decision node) */
    int16_t left_child;       /**< Index of left child (-1 if none) */
    int16_t right_child;      /**< Index of right child (-1 if none) */
    int16_t predicted_class;  /**< Predicted class (if leaf node) */
    uint8_t confidence;       /**< Confidence 0-100 (if leaf node) */
} dt_node_t;

/**
 * @brief Decision tree model
 */
typedef struct {
    dt_node_t *nodes;        /**< Tree nodes array */
    uint16_t num_nodes;      /**< Total number of nodes */
    int16_t root_idx;         /**< Index of root node */
    uint8_t num_classes;      /**< Number of classes */
    uint8_t feature_dim;      /**< Number of features */
} dt_model_t;

/**
 * @brief Inference result
 */
typedef struct {
    int16_t predicted_class;
    uint8_t confidence;
    uint16_t depth_reached;   /**< How deep the inference went */
} dt_result_t;

/**
 * @brief Feature vector
 */
typedef struct {
    int16_t *values;          /**< Feature values [feature_dim], Q8.8 */
    uint8_t num_features;     /**< Number of features */
} dt_features_t;

/**
 * \addtogroup DECISION_TREE_Exported_Functions
 * \{
 */

/**
 * @brief Initialize decision tree model
 * @param model Model handle
 * @param nodes Pre-allocated nodes array
 * @param num_nodes Maximum number of nodes
 * @param num_classes Number of classes
 * @param feature_dim Number of features
 * @return 0 on success
 */
int dt_init(dt_model_t *model, dt_node_t *nodes, uint16_t num_nodes,
            uint8_t num_classes, uint8_t feature_dim);

/**
 * @brief Set root node index
 * @param model Model
 * @param root_idx Root node index
 */
void dt_set_root(dt_model_t *model, int16_t root_idx);

/**
 * @brief Predict class for given features
 * @param model Decision tree model
 * @param features Input features
 * @param result Output result
 * @return 0 on success
 */
int dt_predict(dt_model_t *model, dt_features_t *features, dt_result_t *result);

/**
 * @brief Get human-readable tree traversal path
 * @param model Model
 * @param features Input features
 * @param path_out Output path description (caller allocates)
 * @param path_max_len Max length of path_out
 * @return Number of nodes traversed
 */
int dt_explain(dt_model_t *model, dt_features_t *features, char *path_out, uint16_t path_max_len);

/**
 * @brief Validate tree structure
 * @param model Model
 * @return 0 if valid, negative if invalid
 */
int dt_validate(dt_model_t *model);

/**
 * \}
 */

/**
 * \}
 */

#ifdef __cplusplus
}
#endif

#endif

/************************ Copyright (c) 2026 aiclaw *****END OF FILE****/
