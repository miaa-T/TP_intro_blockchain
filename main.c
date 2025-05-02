#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Simple hash function for demonstration (not cryptographically secure)
void simple_hash(const char* str, char output[65]) {
    unsigned long hash = 5381;
    int c;

    // DJB2 hash algorithm
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    // Convert to hex string (we'll pad it to look like SHA-256)
    snprintf(output, 65, "%016lx%016lx%016lx%016lx", hash, hash, hash, hash);
}

// Transaction structure
typedef struct {
    char sender[50];
    char receiver[50];
    double amount;
    time_t timestamp;
} Transaction;

// Function to create a new transaction
Transaction create_transaction(const char* sender, const char* receiver, double amount) {
    Transaction t;
    strncpy(t.sender, sender, sizeof(t.sender) - 1);
    strncpy(t.receiver, receiver, sizeof(t.receiver) - 1);
    t.amount = amount;
    t.timestamp = time(NULL);
    return t;
}

// Merkle Tree Node structure
typedef struct MerkleNode {
    char hash[65];
    struct MerkleNode* left;
    struct MerkleNode* right;
} MerkleNode;

// Function to hash a transaction
void hash_transaction(const Transaction* t, char output[65]) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s%f%ld",
             t->sender, t->receiver, t->amount, t->timestamp);
    simple_hash(buffer, output);
}

// Function to create a Merkle leaf node
MerkleNode* create_leaf(const Transaction* t) {
    MerkleNode* node = (MerkleNode*)malloc(sizeof(MerkleNode));
    hash_transaction(t, node->hash);
    node->left = NULL;
    node->right = NULL;
    return node;
}

// Function to create a Merkle parent node
MerkleNode* create_parent(MerkleNode* left, MerkleNode* right) {
    MerkleNode* node = (MerkleNode*)malloc(sizeof(MerkleNode));

    // If right is NULL (odd number of nodes), duplicate left
    if (right == NULL) {
        strncpy(node->hash, left->hash, sizeof(node->hash));
    } else {
        char buffer[130]; // Enough for two 64-char hashes + null terminator
        snprintf(buffer, sizeof(buffer), "%s%s", left->hash, right->hash);
        simple_hash(buffer, node->hash);
    }

    node->left = left;
    node->right = right;
    return node;
}

// Function to build a Merkle Tree
MerkleNode* build_merkle_tree(Transaction* transactions, int count) {
    if (count == 0) return NULL;

    // Create leaf nodes
    MerkleNode** nodes = (MerkleNode**)malloc(count * sizeof(MerkleNode*));
    for (int i = 0; i < count; i++) {
        nodes[i] = create_leaf(&transactions[i]);
    }

    int level_size = count;
    while (level_size > 1) {
        int next_level_size = (level_size + 1) / 2;

        for (int i = 0; i < next_level_size; i++) {
            int left = 2 * i;
            int right = left + 1;

            if (right < level_size) {
                nodes[i] = create_parent(nodes[left], nodes[right]);
            } else {
                nodes[i] = create_parent(nodes[left], NULL);
            }
        }

        level_size = next_level_size;
    }

    MerkleNode* root = nodes[0];
    free(nodes);
    return root;
}

// Function to free Merkle Tree memory
void free_merkle_tree(MerkleNode* root) {
    if (root == NULL) return;
    free_merkle_tree(root->left);
    free_merkle_tree(root->right);
    free(root);
}

// Block structure
typedef struct {
    int index;
    time_t timestamp;
    Transaction* transactions;
    int transaction_count;
    char previous_hash[65];
    char hash[65];
    MerkleNode* merkle_root;
} Block;

// Function to calculate block hash
void calculate_block_hash(Block* block) {
    char buffer[1024];

    // Create a string with block data
    snprintf(buffer, sizeof(buffer), "%d%ld%s%s",
             block->index,
             block->timestamp,
             block->previous_hash,
             block->merkle_root ? block->merkle_root->hash : "null");

    simple_hash(buffer, block->hash);
}

// Function to create a new block
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->transaction_count = count;

    // Copy transactions
    block->transactions = (Transaction*)malloc(count * sizeof(Transaction));
    if (!block->transactions) {
        free(block);
        return NULL;
    }
    memcpy(block->transactions, transactions, count * sizeof(Transaction));

    // Build Merkle Tree
    block->merkle_root = build_merkle_tree(block->transactions, block->transaction_count);

    // Calculate block hash
    calculate_block_hash(block);

    return block;
}

// Function to print block information
void print_block(const Block* block) {
    printf("\n=== Block #%d ===\n", block->index);
    printf("Timestamp: %ld\n", block->timestamp);
    printf("Previous Hash: %s\n", block->previous_hash);
    printf("Current Hash: %s\n", block->hash);
    printf("Merkle Root: %s\n", block->merkle_root ? block->merkle_root->hash : "null");
    printf("Number of Transactions: %d\n", block->transaction_count);

    printf("\nTransactions:\n");
    for (int i = 0; i < block->transaction_count; i++) {
        char tx_hash[65];
        hash_transaction(&block->transactions[i], tx_hash);
        printf("[%s] %s -> %s: %.2f at %ld\n",
               tx_hash,
               block->transactions[i].sender,
               block->transactions[i].receiver,
               block->transactions[i].amount,
               block->transactions[i].timestamp);
    }
    printf("\n");
}

// Function to free block memory
void free_block(Block* block) {
    if (block) {
        free_merkle_tree(block->merkle_root);
        free(block->transactions);
        free(block);
    }
}

// Function to demonstrate block tampering
void demonstrate_tampering(Block* block) {
    printf("\nAttempting to tamper with block #%d...\n", block->index);

    // Change a transaction
    if (block->transaction_count > 0) {
        printf("Changing transaction amount from %.2f to %.2f\n",
               block->transactions[0].amount, 1000.0);
        block->transactions[0].amount = 1000.0;

        // Rebuild Merkle Tree and recalculate hash
        free_merkle_tree(block->merkle_root);
        block->merkle_root = build_merkle_tree(block->transactions, block->transaction_count);
        calculate_block_hash(block);

        printf("New block hash: %s\n", block->hash);
    }
}

int main() {
    printf("=== Simple Blockchain Implementation ===\n");

    Transaction genesis_transactions[3] = {
        create_transaction("Alice", "Bob", 5.0),
        create_transaction("Bob", "Charlie", 2.5),
        create_transaction("Charlie", "Alice", 1.0)
    };

    Block* genesis = create_block(0, "0", genesis_transactions, 3);
    print_block(genesis);

    Transaction second_block_transactions[2] = {
        create_transaction("Dave", "Eve", 3.0),
        create_transaction("Eve", "Alice", 1.5)
    };

    Block* second_block = create_block(1, genesis->hash, second_block_transactions, 2);
    print_block(second_block);

    demonstrate_tampering(second_block);

    printf("\nBlockchain after tampering:\n");
    print_block(genesis);
    print_block(second_block);

    // Free memory
    free_block(genesis);
    free_block(second_block);

    return 0;
}