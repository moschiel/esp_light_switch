#ifndef MAIN_MESH_TREE_H_
#define MAIN_MESH_TREE_H_

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

// Definindo a estrutura
typedef struct {
    uint8_t node_sta_addr[6];
    uint8_t parent_sta_addr[6];
    int layer;
} MeshNode;

//void start_tree_monitor(void);
void update_tree_with_node(uint8_t node_sta_addr[6], uint8_t parent_sta_addr[6], int layer);
void remove_node_from_tree(uint8_t node_sta_addr[6]);
void remove_non_existing_node_from_tree(void);
void clear_node_tree(void);
MeshNode* get_mesh_tree_table(int *mesh_tree_count);
char* build_mesh_tree_json(void);

#endif /* MAIN_MESH_TABLE_H_ */
