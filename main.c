#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// Simple hash function (same as before)
void simple_hash(const char* str, char output[65]) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    snprintf(output, 65, "%016lx%016lx%016lx%016lx", hash, hash, hash, hash);
}

// Transaction structure
typedef struct {
    char sender[50];
    char receiver[50];
    double amount;
    time_t timestamp;
} Transaction;

// Block structure
typedef struct Block {
    int index;
    time_t timestamp;
    Transaction* transactions;
    int transaction_count;
    char previous_hash[65];
    char hash[65];
    struct Block* next;
} Block;

// Blockchain structure
typedef struct {
    Block* head;
    Block* tail;
    int length;
    pthread_mutex_t lock;
} Blockchain;

// Node structure (represents a network node)
typedef struct {
    int id;
    Blockchain blockchain;
    pthread_t thread;
    int running;
} Node;

// Network of nodes
#define MAX_NODES 5
Node network[MAX_NODES];
int node_count = 0;

// Function prototypes
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count);
void add_block(Blockchain* blockchain, Block* block);
void print_blockchain(const Blockchain* blockchain);
void* node_process(void* arg);
void broadcast_block(int sender_id, Block* block);

// Create a new block
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    block->transaction_count = count;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);

    // Copy transactions
    block->transactions = (Transaction*)malloc(count * sizeof(Transaction));
    if (!block->transactions) {
        free(block);
        return NULL;
    }
    memcpy(block->transactions, transactions, count * sizeof(Transaction));

    // Calculate block hash (simplified)
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%d%ld%s%d", index, block->timestamp, previous_hash, count);
    simple_hash(buffer, block->hash);

    block->next = NULL;
    return block;
}

// Add block to blockchain
void add_block(Blockchain* blockchain, Block* block) {
    pthread_mutex_lock(&blockchain->lock);

    if (blockchain->head == NULL) {
        blockchain->head = block;
        blockchain->tail = block;
    } else {
        blockchain->tail->next = block;
        blockchain->tail = block;
    }
    blockchain->length++;

    pthread_mutex_unlock(&blockchain->lock);
}

// Print blockchain
void print_blockchain(const Blockchain* blockchain) {
    Block* current = blockchain->head;
    printf("Blockchain (length: %d):\n", blockchain->length);
    while (current != NULL) {
        printf("[%d] %s -> %s (Prev: %s)\n",
               current->index, current->hash, current->previous_hash);
        current = current->next;
    }
    printf("\n");
}

// Node process function (runs in a thread)
void* node_process(void* arg) {
    Node* node = (Node*)arg;
    printf("Node %d started\n", node->id);

    while (node->running) {
        // Simulate occasional block creation
        if (rand() % 10 == 0 && node->blockchain.length < 5) {
            // Create a new block
            Transaction tx = {
                .sender = "Node",
                .receiver = "Network",
                .amount = rand() % 100,
                .timestamp = time(NULL)
            };

            const char* prev_hash = node->blockchain.tail ? node->blockchain.tail->hash : "0";
            Block* new_block = create_block(node->blockchain.length, prev_hash, &tx, 1);

            if (new_block) {
                add_block(&node->blockchain, new_block);
                printf("Node %d created block %d\n", node->id, new_block->index);

                // Broadcast to other nodes
                broadcast_block(node->id, new_block);
            }
        }

        // Sleep for a bit
        struct timespec ts = {0, 100000000}; // 100ms
        nanosleep(&ts, NULL);
    }

    printf("Node %d stopped\n", node->id);
    return NULL;
}

// Broadcast block to other nodes
void broadcast_block(int sender_id, Block* block) {
    for (int i = 0; i < node_count; i++) {
        if (i != sender_id) {
            // In a real system, this would be network communication
            // Here we just add directly with some random delay/loss

            // Simulate network delay (0-200ms)
            struct timespec ts = {0, (rand() % 200) * 1000000};
            nanosleep(&ts, NULL);

            // Simulate 10% packet loss
            if (rand() % 10 < 1) {
                continue;
            }

            // Add the block to the receiving node's blockchain
            Block* block_copy = create_block(block->index, block->previous_hash,
                                          block->transactions, block->transaction_count);
            if (block_copy) {
                add_block(&network[i].blockchain, block_copy);
                printf("Block %d propagated from node %d to node %d\n",
                      block->index, sender_id, i);
            }
        }
    }
}

// Initialize a node
void init_node(int id) {
    if (node_count >= MAX_NODES) return;

    network[node_count].id = id;
    network[node_count].blockchain.head = NULL;
    network[node_count].blockchain.tail = NULL;
    network[node_count].blockchain.length = 0;
    network[node_count].running = 1;
    pthread_mutex_init(&network[node_count].blockchain.lock, NULL);

    // Create genesis block for this node
    Transaction genesis_tx = {
        .sender = "System",
        .receiver = "Genesis",
        .amount = 0,
        .timestamp = time(NULL)
    };
    Block* genesis = create_block(0, "0", &genesis_tx, 1);
    add_block(&network[node_count].blockchain, genesis);

    pthread_create(&network[node_count].thread, NULL, node_process, &network[node_count]);
    node_count++;
}

// Stop all nodes
void stop_nodes() {
    for (int i = 0; i < node_count; i++) {
        network[i].running = 0;
        pthread_join(network[i].thread, NULL);
        pthread_mutex_destroy(&network[i].blockchain.lock);
    }
}

int main() {
    srand(time(NULL));

    printf("=== Blockchain Network Simulation ===\n");

    // Initialize nodes
    for (int i = 0; i < 3; i++) {
        init_node(i);
    }

    // Let the simulation run for 5 seconds
    sleep(5);

    // Stop nodes
    stop_nodes();

    // Print final blockchains
    printf("\nFinal blockchains:\n");
    for (int i = 0; i < node_count; i++) {
        printf("\nNode %d blockchain:\n", i);
        print_blockchain(&network[i].blockchain);
    }

    return 0;
}