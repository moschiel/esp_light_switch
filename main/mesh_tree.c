#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "cJSON.h"

#include "mesh_tree.h"
#include "utils.h"
#include "mesh_network.h"

static void add_node_to_json(cJSON *parent, MeshNode *node, int layer);


static char* TAG = "MESH_TREE";
static int tree_node_count = 0;
static SemaphoreHandle_t xMutexTree;
//static bool tree_monitor_started = false;

static MeshNode _mesh_tree_[CONFIG_MESH_ROUTE_TABLE_SIZE] = {0};
static mesh_addr_t routing_table[CONFIG_MESH_ROUTE_TABLE_SIZE];

extern uint8_t STA_MAC_address[6];

static bool take_tree_mutex(char *source) {
    if(xMutexTree == NULL) {
        xMutexTree = xSemaphoreCreateMutex();
    }
    if(xSemaphoreTake(xMutexTree, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Fail to Take Tree Mutex: '%s'", source);
        return false;
    }

    return true;
} 

static void give_tree_mutex() {
    xSemaphoreGive(xMutexTree);
}

static void log_tree(void) {
    ESP_LOGW(TAG, "Tree Updated, Node Count: %i", tree_node_count + 1);
    ESP_LOGW(TAG, "[1] root:"MACSTREND", layer:1", MAC2STREND(STA_MAC_address));
    for (size_t i = 0; i < tree_node_count; i++) {
        ESP_LOGW(TAG, "[%i] node:"MACSTREND", parent:"MACSTREND", layer:%i", 
            (i+2), MAC2STREND(_mesh_tree_[i].node_sta_addr), MAC2STREND(_mesh_tree_[i].parent_sta_addr), _mesh_tree_[i].layer);
    }
}

void update_tree_with_node(uint8_t node_sta_addr[6], uint8_t parent_sta_addr[6], int layer) {
    if (take_tree_mutex("update_tree_with_node")) {
        // Verifica se a combinação já existe no array ou se o node_sta_addr já existe
        bool node_exist = false;
        bool updated = false;
        for (size_t i = 0; i < tree_node_count; i++) {
            if (compare_mac(_mesh_tree_[i].node_sta_addr, node_sta_addr)) {
                // Se o node_sta_addr já existe, 
                node_exist = true;
                if(compare_mac(_mesh_tree_[i].parent_sta_addr, parent_sta_addr) == false || _mesh_tree_[i].layer != layer) {
                    //existe mas mudou o parent ou o layer, entao sobrescreve
                    memcpy(_mesh_tree_[i].parent_sta_addr, parent_sta_addr, 6);
                    _mesh_tree_[i].layer = layer;
                    updated = true;
                }
            }
        }

        // Se o node não existe e há espaço no array, adiciona o novo item
        if (node_exist == false && tree_node_count < CONFIG_MESH_ROUTE_TABLE_SIZE) {
            memcpy(_mesh_tree_[tree_node_count].node_sta_addr, node_sta_addr, 6);
            memcpy(_mesh_tree_[tree_node_count].parent_sta_addr, parent_sta_addr, 6);
            _mesh_tree_[tree_node_count].layer = layer;
            tree_node_count++;
            updated = true;
        }

        if(updated) {
            log_tree();
        }

        give_tree_mutex();
    }
}

void remove_node_from_tree(uint8_t node_sta_addr[6]) {
    size_t new_count = 0;

    for (size_t i = 0; i < tree_node_count; i++) {
        if (!compare_mac(_mesh_tree_[i].node_sta_addr, node_sta_addr)) {
            // Mantém o node no array se ele não é o que deve ser removido
            if (new_count != i) {
                _mesh_tree_[new_count] = _mesh_tree_[i];
            }
            new_count++;
        }
    }

    //bool updated = node_count != new_count;
    tree_node_count = new_count;
    //if(updated) log_tree();
}

void clear_node_tree(void) {
    tree_node_count = 0;
}

void remove_non_existing_node_from_tree(void) {
    if (take_tree_mutex("remove_non_existing_node_from_tree")) {
        int routing_table_size = 0;
        esp_mesh_get_routing_table((mesh_addr_t *)&routing_table, CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &routing_table_size);
        
        bool updated = false;
        int starting_index = 0;
SCAN_REMOVE_NODE:
        for(int i=starting_index; i<tree_node_count; i++) {
            bool exist_on_tree = false;
            for(int k=0; k<routing_table_size;k++) {
                if(compare_mac(_mesh_tree_[i].node_sta_addr, routing_table[k].addr)) {
                    exist_on_tree = true;
                    break;
                }
            }
            if(exist_on_tree == false) {
                remove_node_from_tree(_mesh_tree_[i].node_sta_addr);
                updated = true;
                starting_index = i;
                goto SCAN_REMOVE_NODE;
            }
        }

        if(updated) {
            log_tree();
        }

        give_tree_mutex();
    }
}

MeshNode* get_mesh_tree_table(int *mesh_tree_count) {
    if(take_tree_mutex("get_mesh_tree_table")) {
        MeshNode *ret = malloc(sizeof(MeshNode) * tree_node_count);
        memcpy(ret, &_mesh_tree_, sizeof(MeshNode) * tree_node_count);
        *mesh_tree_count = tree_node_count;
        
        give_tree_mutex();
        return ret;
    }

    return NULL;
}

static void add_node_to_json(cJSON *parent, MeshNode *node, int layer) {
    char node_mac_str[9];
    format_mac(node_mac_str, node->node_sta_addr);
    
    cJSON *node_json = cJSON_CreateObject();
    cJSON_AddStringToObject(node_json, "name", node_mac_str);
    cJSON_AddNumberToObject(node_json, "layer", layer);

    cJSON *node_children;
    bool has_children = false;

    for (int i = 0; i < tree_node_count; i++) {
        if (memcmp(_mesh_tree_[i].parent_sta_addr, node->node_sta_addr, 6) == 0) {
            if(has_children == false) {
                has_children = true;
                node_children = cJSON_AddArrayToObject(node_json, "children");
            }
            add_node_to_json(node_children, &_mesh_tree_[i], layer + 1);
        }
    }

    // Add Node (Layer + 1) as a child of Parent Node (Layer)
    cJSON_AddItemToArray(parent, node_json);
}

// Função para criar o JSON da árvore de rede mesh
char* build_mesh_tree_json(void) {
    if (take_tree_mutex("build_mesh_tree_json")) {
        // Função auxiliar para converter o endereço MAC para a string com os três últimos bytes
        char mac_str[9];
        format_mac(mac_str, STA_MAC_address);

        //WiFi Router Node at layer 0
        cJSON *router_node = cJSON_CreateObject();
        cJSON_AddStringToObject(router_node, "name", "WiFi Router");
        cJSON_AddNumberToObject(router_node, "layer", 0);
        cJSON *router_children = cJSON_AddArrayToObject(router_node, "children");

        //Root Node at layer 1
        cJSON *root_node = cJSON_CreateObject();
        cJSON_AddNumberToObject(root_node, "layer", 1);
        cJSON_AddStringToObject(root_node, "name", mac_str);
        
        //Add Other Nodes (Starting at Layer 2) as children of Root Node (Layer 1)
        cJSON *root_node_children;
        bool has_children = false;
        for (int i = 0; i < tree_node_count; i++) {
            if (memcmp(_mesh_tree_[i].parent_sta_addr, STA_MAC_address, 6) == 0) {
                if(has_children == false) {
                    has_children = true;
                    root_node_children = cJSON_AddArrayToObject(root_node, "children");
                }
                add_node_to_json(root_node_children, &_mesh_tree_[i], 2);
            }
        }

        // Add Root Node (Layer 1) as a child of Wifi Router Node (Layer 0)
        cJSON_AddItemToArray(router_children, root_node);

        char* ret = cJSON_PrintUnformatted(router_node); //json em uma unica linha
        //char* ret = cJSON_Print(router); //tem endentacao e pula linha, mas fica maior

        // Limpar memória alocada para o JSON
        cJSON_Delete(router_node);

        give_tree_mutex();
        // quem usar esse retorno, deve lembrar de dar free nele
        return ret;
    }

    return NULL;
}

/*
static void monitor_tree_task(void *arg) {
    if(xMutexTree == NULL) {
        xMutexTree = xSemaphoreCreateMutex();
    }

    ESP_LOGW(TAG, "Started Tree Task");
    
    while(1) {
        if (xSemaphoreTake(xMutexTree, portMAX_DELAY) == pdTRUE) {
            if(esp_mesh_is_root() && is_mesh_parent_connected()) {
                remove_non_existing_node_from_tree();
            } else {
                clear_tree();
                break; //sai do while, e vair deletar a tarefa
            }
            
        }
        vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGW(TAG, "Deleted Tree Task");
    tree_monitor_started = false;
    vTaskDelete(NULL);
}

void start_tree_monitor(void) {
    if (!tree_monitor_started) {
        tree_monitor_started = true;
        xTaskCreate(monitor_tree_task, "monitor_tree_task", 1024, NULL, 5, NULL);

    }
}
*/