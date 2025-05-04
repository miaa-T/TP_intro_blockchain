#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>

// Improved hash function (simplified SHA-256 like)
void simple_hash(const char* str, char output[65]) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    snprintf(output, 65, "%016lx%016lx%016lx%016lx", hash, hash, hash, hash);
}

typedef struct {
    char sender[50];
    char receiver[50];
    double amount;
    time_t timestamp;
    char signature[65];
} Transaction;

typedef struct {
    char address[50];
    double balance;
} Account;

#define MAX_ACCOUNTS 100
Account accounts[MAX_ACCOUNTS];
int account_count = 0;
pthread_mutex_t accounts_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct Block {
    int index;
    time_t timestamp;
    Transaction* transactions;
    int transaction_count;
    char previous_hash[65];
    char hash[65];
    int nonce;
    struct Block* next;
} Block;

typedef struct {
    Block* head;
    Block* tail;
    int length;
    pthread_mutex_t lock;
} Blockchain;

typedef struct {
    int id;
    Blockchain blockchain;
    pthread_t thread;
    int running;
    bool is_malicious;
} Node; // pour le merkel tree

#define MAX_NODES 5
Node network[MAX_NODES];
int node_count = 0;

#define MAX_PENDING_TRANSACTIONS 10
Transaction pending_transactions[MAX_PENDING_TRANSACTIONS];
int pending_transaction_count = 0;
pthread_mutex_t pending_transactions_lock = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count);
void add_block(Blockchain* blockchain, Block* block);
void print_blockchain(const Blockchain* blockchain);
void* node_process(void* arg);
void broadcast_block(int sender_id, Block* block);
bool validate_transaction(Transaction* tx);
bool validate_block(Block* block, const char* previous_hash);
void mine_block(Block* block);
Block* create_malicious_block(int index, const char* previous_hash, Transaction* transactions, int count);
void add_transaction_to_pool(Transaction tx);
void process_pending_transactions(Node* node);
void init_accounts();
void update_account_balances(Transaction* tx);
void sign_transaction(Transaction* tx);

// Consensus difficulty (5 leading zeros)
#define CONSENSUS_TARGET "00000"

// Create a new block (updated to include mining)
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    block->transaction_count = count;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->nonce = 0;

    block->transactions = (Transaction*)malloc(count * sizeof(Transaction));
    if (!block->transactions) {
        free(block);
        return NULL;
    }
    memcpy(block->transactions, transactions, count * sizeof(Transaction));

    // Mining process happens here
    mine_block(block);

    block->next = NULL;
    return block;
}

// Create a malicious block (doesn't follow consensus rules)
Block* create_malicious_block(int index, const char* previous_hash, Transaction* transactions, int count) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    block->transaction_count = count;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->nonce = 0;

    block->transactions = (Transaction*)malloc(count * sizeof(Transaction));
    if (!block->transactions) {
        free(block);
        return NULL;
    }
    memcpy(block->transactions, transactions, count * sizeof(Transaction));

    // Malicious node doesn't mine properly, just sets a fake hash
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%d%ld%s%d", index, block->timestamp, previous_hash, block->nonce);
    simple_hash(buffer, block->hash);

    // Forge the hash to look valid without actually mining
    memcpy(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET));

    block->next = NULL;
    return block;
}

// Mine a block (implement proof of work)
void mine_block(Block* block) {
    char buffer[1024];
    bool found = false;

    while (!found) {
        snprintf(buffer, sizeof(buffer), "%d%ld%s%d%d",
                 block->index, block->timestamp, block->previous_hash,
                 block->transaction_count, block->nonce);

        simple_hash(buffer, block->hash);

        // Check if hash meets consensus requirement (starts with 5 zeros)
        if (strncmp(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET)) == 0) {
            found = true;
        } else {
            block->nonce++;
        }
    }

    printf("Block mined with nonce: %d, hash: %s\n", block->nonce, block->hash);
}

// Add block to the blockchain
void add_block(Blockchain* blockchain, Block* block) {
    pthread_mutex_lock(&blockchain->lock);

    if (blockchain->head == NULL) {
        blockchain->head = block;
        blockchain->tail = block;
    } else {
        // Verify block before adding
        if (validate_block(block, blockchain->tail->hash)) {
            blockchain->tail->next = block;
            blockchain->tail = block;
            blockchain->length++;

            // Update account balances
            for (int i = 0; i < block->transaction_count; i++) {
                update_account_balances(&block->transactions[i]);
            }
        } else {
            printf("Invalid block rejected: %d\n", block->index);
            free(block->transactions);
            free(block);
            pthread_mutex_unlock(&blockchain->lock);
            return;
        }
    }

    pthread_mutex_unlock(&blockchain->lock);
}

// Validate a block
bool validate_block(Block* block, const char* previous_hash) {
    // 1. Check previous hash matches
    if (strcmp(block->previous_hash, previous_hash) != 0) {
        printf("Previous hash mismatch\n");
        return false;
    }

    // 2. Verify hash starts with the target number of zeros
    if (strncmp(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET)) != 0) {
        printf("Block hash doesn't meet consensus requirements\n");
        return false;
    }

    // 3. Recalculate hash to verify it's correct
    char buffer[1024];
    char calculated_hash[65];

    snprintf(buffer, sizeof(buffer), "%d%ld%s%d%d",
             block->index, block->timestamp, block->previous_hash,
             block->transaction_count, block->nonce);

    simple_hash(buffer, calculated_hash);

    if (strcmp(calculated_hash, block->hash) != 0) {
        printf("Block hash verification failed\n");
        return false;
    }

    // 4. Verify all transactions in the block are valid
    for (int i = 0; i < block->transaction_count; i++) {
        if (!validate_transaction(&block->transactions[i])) {
            printf("Invalid transaction in block\n");
            return false;
        }
    }

    return true;
}

// Sign a transaction (simplified)
void sign_transaction(Transaction* tx) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s%.2f%ld",
             tx->sender, tx->receiver, tx->amount, tx->timestamp);
    simple_hash(buffer, tx->signature);
}

// Validate a transaction
bool validate_transaction(Transaction* tx) {
    // Check signature
    char buffer[256];
    char calculated_signature[65];

    snprintf(buffer, sizeof(buffer), "%s%s%.2f%ld",
             tx->sender, tx->receiver, tx->amount, tx->timestamp);
    simple_hash(buffer, calculated_signature);

    if (strcmp(calculated_signature, tx->signature) != 0) {
        printf("Transaction signature invalid\n");
        return false;
    }

    // Check sender has enough funds
    pthread_mutex_lock(&accounts_lock);
    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].address, tx->sender) == 0) {
            if (accounts[i].balance < tx->amount) {
                pthread_mutex_unlock(&accounts_lock);
                printf("Insufficient funds: %s has only %.2f\n", tx->sender, accounts[i].balance);
                return false;
            }
            pthread_mutex_unlock(&accounts_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&accounts_lock);

    // If we're sending from "System" for genesis, allow it
    if (strcmp(tx->sender, "System") == 0) {
        return true;
    }

    printf("Sender account not found: %s\n", tx->sender);
    return false;
}

// Update account balances based on transaction
void update_account_balances(Transaction* tx) {
    pthread_mutex_lock(&accounts_lock);

    // Skip system transactions
    if (strcmp(tx->sender, "System") == 0) {
        // Find receiver account or create it
        bool receiver_found = false;
        for (int i = 0; i < account_count; i++) {
            if (strcmp(accounts[i].address, tx->receiver) == 0) {
                accounts[i].balance += tx->amount;
                receiver_found = true;
                break;
            }
        }

        if (!receiver_found && account_count < MAX_ACCOUNTS) {
            strcpy(accounts[account_count].address, tx->receiver);
            accounts[account_count].balance = tx->amount;
            account_count++;
        }

        pthread_mutex_unlock(&accounts_lock);
        return;
    }

    // Handle regular transactions
    bool sender_found = false;
    bool receiver_found = false;

    for (int i = 0; i < account_count; i++) {
        if (strcmp(accounts[i].address, tx->sender) == 0) {
            accounts[i].balance -= tx->amount;
            sender_found = true;
        }

        if (strcmp(accounts[i].address, tx->receiver) == 0) {
            accounts[i].balance += tx->amount;
            receiver_found = true;
        }
    }

    // Create receiver account if not found
    if (!receiver_found && account_count < MAX_ACCOUNTS) {
        strcpy(accounts[account_count].address, tx->receiver);
        accounts[account_count].balance = tx->amount;
        account_count++;
    }

    pthread_mutex_unlock(&accounts_lock);
}

// Initialize accounts with some test balances
void init_accounts() {
    pthread_mutex_lock(&accounts_lock);

    strcpy(accounts[0].address, "Node0");
    accounts[0].balance = 100.0;

    strcpy(accounts[1].address, "Node1");
    accounts[1].balance = 100.0;

    strcpy(accounts[2].address, "Node2");
    accounts[2].balance = 100.0;

    account_count = 3;

    pthread_mutex_unlock(&accounts_lock);
}

// Print the blockchain
void print_blockchain(const Blockchain* blockchain) {
    Block* current = blockchain->head;
    printf("Blockchain (length: %d):\n", blockchain->length);
    while (current != NULL) {
        printf("[%d] Hash: %s\n", current->index, current->hash);
        printf("    Prev: %s\n", current->previous_hash);
        printf("    Nonce: %d\n", current->nonce);
        printf("    Transactions: %d\n", current->transaction_count);

        for (int i = 0; i < current->transaction_count; i++) {
            printf("    - %s -> %s: %.2f\n",
                  current->transactions[i].sender,
                  current->transactions[i].receiver,
                  current->transactions[i].amount);
        }

        current = current->next;
    }
    printf("\n");
}

// Add transaction to the pending pool
void add_transaction_to_pool(Transaction tx) {
    pthread_mutex_lock(&pending_transactions_lock);

    if (pending_transaction_count < MAX_PENDING_TRANSACTIONS) {
        // Sign the transaction
        sign_transaction(&tx);

        if (validate_transaction(&tx)) {
            memcpy(&pending_transactions[pending_transaction_count], &tx, sizeof(Transaction));
            pending_transaction_count++;
            printf("Transaction added to pool: %s -> %s: %.2f\n", tx.sender, tx.receiver, tx.amount);
        } else {
            printf("Invalid transaction rejected: %s -> %s: %.2f\n", tx.sender, tx.receiver, tx.amount);
        }
    }

    pthread_mutex_unlock(&pending_transactions_lock);
}

// Process pending transactions
void process_pending_transactions(Node* node) {
    pthread_mutex_lock(&pending_transactions_lock);

    if (pending_transaction_count > 0) {
        int count = pending_transaction_count > 5 ? 5 : pending_transaction_count;
        Transaction* transactions = (Transaction*)malloc(count * sizeof(Transaction));

        if (transactions) {
            memcpy(transactions, pending_transactions, count * sizeof(Transaction));

            // Create a new block with these transactions
            const char* prev_hash = node->blockchain.tail ? node->blockchain.tail->hash : "0";
            Block* new_block;

            if (node->is_malicious) {
                printf("Node %d (malicious) creating block with %d transactions\n", node->id, count);
                new_block = create_malicious_block(node->blockchain.length, prev_hash, transactions, count);
            } else {
                printf("Node %d creating block with %d transactions\n", node->id, count);
                new_block = create_block(node->blockchain.length, prev_hash, transactions, count);
            }

            free(transactions);

            if (new_block) {
                add_block(&node->blockchain, new_block);
                printf("Node %d created block %d\n", node->id, new_block->index);

                // Broadcast to other nodes
                broadcast_block(node->id, new_block);

                // Remove processed transactions from pool
                memmove(pending_transactions,
                        &pending_transactions[count],
                        (pending_transaction_count - count) * sizeof(Transaction));
                pending_transaction_count -= count;
            }
        }
    }

    pthread_mutex_unlock(&pending_transactions_lock);
}

// Node process function
void* node_process(void* arg) {
    Node* node = (Node*)arg;
    printf("Node %d started (malicious: %s)\n", node->id, node->is_malicious ? "yes" : "no");

    while (node->running) {
        if (rand() % 10 == 0) {
            Transaction tx;
            sprintf(tx.sender, "Node%d", node->id);
            sprintf(tx.receiver, "Node%d", rand() % node_count);
            tx.amount = (rand() % 10) + 1;
            tx.timestamp = time(NULL);

            add_transaction_to_pool(tx);
        }

        process_pending_transactions(node);

        struct timespec ts = {0, 500000000}; // 500ms
        nanosleep(&ts, NULL);
    }

    printf("Node %d stopped\n", node->id);
    return NULL;
}

// Broadcast a block to other nodes
void broadcast_block(int sender_id, Block* block) {
    for (int i = 0; i < node_count; i++) {
        if (i != sender_id) {

            struct timespec ts = {0, (rand() % 200) * 1000000};//communication latency zaema
            nanosleep(&ts, NULL);

            if (rand() % 10 < 1) {
                continue;
            }

            // Clone the block for the receiving node
            Block* block_copy = (Block*)malloc(sizeof(Block));
            if (!block_copy) continue;

            memcpy(block_copy, block, sizeof(Block));

            // Deep copy transactions
            block_copy->transactions = (Transaction*)malloc(block->transaction_count * sizeof(Transaction));
            if (!block_copy->transactions) {
                free(block_copy);
                continue;
            }

            memcpy(block_copy->transactions, block->transactions, block->transaction_count * sizeof(Transaction));
            block_copy->next = NULL;

            // Add the block to the receiving node's blockchain (validation happens in add_block)
            add_block(&network[i].blockchain, block_copy);
            printf("Block %d propagated from node %d to node %d\n", block->index, sender_id, i);
        }
    }
}

// Initialize a new node
void init_node(int id, bool is_malicious) {
    if (node_count >= MAX_NODES) return;

    network[node_count].id = id;
    network[node_count].is_malicious = is_malicious;
    network[node_count].blockchain.head = NULL;
    network[node_count].blockchain.tail = NULL;
    network[node_count].blockchain.length = 0;
    network[node_count].running = 1;
    pthread_mutex_init(&network[node_count].blockchain.lock, NULL);

    // Create genesis block for this node
    Transaction genesis_tx = {
        .sender = "mia",
        .receiver = "mahdia",
        .amount = 100.0,
        .timestamp = time(NULL)
    };
    sign_transaction(&genesis_tx);

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

// Print all account balances
void print_account_balances() {
    pthread_mutex_lock(&accounts_lock);

    printf("\n=== Account Balances ===\n");
    for (int i = 0; i < account_count; i++) {
        printf("%s: %.2f coins\n", accounts[i].address, accounts[i].balance);
    }

    pthread_mutex_unlock(&accounts_lock);
}

// Read transactions from a specific block
void read_block_transactions(const Blockchain* blockchain, int block_index) {
    Block* current = blockchain->head;

    while (current != NULL) {
        if (current->index == block_index) {
            printf("\n=== Transactions in Block %d ===\n", block_index);
            for (int i = 0; i < current->transaction_count; i++) {
                printf("[%d] %s -> %s: %.2f\n",
                      i,
                      current->transactions[i].sender,
                      current->transactions[i].receiver,
                      current->transactions[i].amount);
            }
            return;
        }
        current = current->next;
    }

    printf("Block %d not found\n", block_index);
}

// TEST 1: Add a valid transaction
void test_add_valid_transaction() {
    printf("\n=== TEST 1: Adding a valid transaction ===\n");

    Transaction tx = {
        .sender = "Node0",
        .receiver = "Node1",
        .amount = 10.0,
        .timestamp = time(NULL)
    };

    add_transaction_to_pool(tx);

    // Let the system process it
    sleep(2);

    print_account_balances();
}

// TEST 2: Add an invalid transaction (insufficient funds)
void test_add_invalid_transaction() {
    printf("\n=== TEST 2: Adding an invalid transaction (insufficient funds) ===\n");

    Transaction tx = {
        .sender = "Node2",
        .receiver = "Node0",
        .amount = 1000.0, // More than available balance
        .timestamp = time(NULL)
    };

    add_transaction_to_pool(tx);

    // Let the system process it
    sleep(2);

    print_account_balances();
}

// TEST 3: Malicious miner test
void test_malicious_miner() {
    printf("\n=== TEST 3: Malicious miner attempting to add invalid blocks ===\n");

    Transaction tx = {
        .sender = "Node1",
        .receiver = "Node2",
        .amount = 5.0,
        .timestamp = time(NULL)
    };

    add_transaction_to_pool(tx);

    // Let the system process it
    sleep(5);

    // Compare blockchains
    printf("\nComparing blockchains after malicious activity:\n");
    for (int i = 0; i < node_count; i++) {
        printf("\nNode %d blockchain (malicious: %s):\n",
              i, network[i].is_malicious ? "yes" : "no");
        print_blockchain(&network[i].blockchain);
    }
}

int main() {
    srand(time(NULL));

    printf("=== Blockchain Network Simulation with Consensus ===\n");

    // Initialize accounts
    init_accounts();

    // Initialize regular nodes
    init_node(0, false);
    init_node(1, false);

    // Initialize a malicious node
    init_node(2, true);

    // Let the system run for a bit
    printf("\nRunning network simulation for 3 seconds...\n");
    sleep(3);

    // Test 1: Add a valid transaction
    test_add_valid_transaction();

    // Test 2: Add an invalid transaction
    test_add_invalid_transaction();

    // Test 3: Malicious miner test
    test_malicious_miner();

    // Read transactions from a block
    printf("\nReading transactions from blockchain:\n");
    read_block_transactions(&network[0].blockchain, 1);

    // Stop simulation
    stop_nodes();

    printf("\nFinal blockchains:\n");
    for (int i = 0; i < node_count; i++) {
        printf("\nNode %d blockchain (malicious: %s):\n", i, network[i].is_malicious ? "yes" : "no");
        print_blockchain(&network[i].blockchain);
    }

    print_account_balances();

    return 0;
}