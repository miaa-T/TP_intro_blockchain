#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#define NUM_NODES 8
#define TRANSACTIONS_PER_BLOCK 3
#define REWARD_AMOUNT 1.0
#define INITIAL_BALANCE 100.0

typedef struct {
    char sender[50];
    char receiver[50];
    double amount;
    time_t timestamp;
} Transaction;

typedef struct {
    char address[50];
    double balance;
} Account;

typedef struct Block {
    int index;
    time_t timestamp;
    Transaction transactions[TRANSACTIONS_PER_BLOCK];
    char previous_hash[65];
    char hash[65];
    struct Block* next;
} Block;

typedef struct {
    Block* head;
    Block* tail;
    int length;
    long current_proof;  // Moved proof to blockchain level
    pthread_mutex_t lock;
} Blockchain;

typedef struct {
    int id;
    Blockchain blockchain;
    pthread_t thread;
    bool running;
    double total_rewards;
    bool is_malicious;
} Node;

Account accounts[NUM_NODES];
Transaction pending_transactions[TRANSACTIONS_PER_BLOCK];
int pending_transaction_count = 0;
pthread_mutex_t transaction_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t transaction_cond = PTHREAD_COND_INITIALIZER;

Node network[NUM_NODES];
bool mining = false;
bool block_found = false;
pthread_mutex_t mining_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t balance_lock = PTHREAD_MUTEX_INITIALIZER;

void simple_hash(const char* str, char output[65]) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    snprintf(output, 65, "%016lx%016lx%016lx%016lx", hash, hash, hash, hash);
}

long calculate_next_proof(long last_proof) {
    long proof = last_proof;
    while (true) {
        proof++;
        if ((proof % 2 != 0) && (proof % 3 == 0)) {
            return proof;
        }
    }
}

Block* create_genesis_block() {
    Block* block = (Block*)malloc(sizeof(Block));
    block->index = 0;
    block->timestamp = time(NULL);
    strcpy(block->previous_hash, "0");
    block->next = NULL;

    // Initialize empty transactions
    for (int i = 0; i < TRANSACTIONS_PER_BLOCK; i++) {
        strcpy(block->transactions[i].sender, "");
        strcpy(block->transactions[i].receiver, "");
        block->transactions[i].amount = 0;
        block->transactions[i].timestamp = 0;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%d%ld%s",
             block->index, block->timestamp, block->previous_hash);
    simple_hash(buffer, block->hash);

    return block;
}

Block* create_block(int index, const char* previous_hash, Transaction txs[TRANSACTIONS_PER_BLOCK], long proof) {
    Block* block = (Block*)malloc(sizeof(Block));
    block->index = index;
    block->timestamp = time(NULL);
    memcpy(block->transactions, txs, sizeof(Transaction) * TRANSACTIONS_PER_BLOCK);
    strcpy(block->previous_hash, previous_hash);
    block->next = NULL;

    char buffer[2048];
    char tx_data[1024] = "";
    for (int i = 0; i < TRANSACTIONS_PER_BLOCK; i++) {
        char temp[128];
        snprintf(temp, sizeof(temp), "%s%s%.2f",
                txs[i].sender, txs[i].receiver, txs[i].amount);
        strcat(tx_data, temp);
    }

    snprintf(buffer, sizeof(buffer), "%d%ld%s%ld%s",
             block->index, block->timestamp, block->previous_hash, proof, tx_data);
    simple_hash(buffer, block->hash);

    return block;
}

bool validate_transaction(Transaction tx) {
    pthread_mutex_lock(&balance_lock);
    bool valid = false;
    for (int i = 0; i < NUM_NODES; i++) {
        if (strcmp(accounts[i].address, tx.sender) == 0) {
            valid = (accounts[i].balance >= tx.amount);
            break;
        }
    }
    pthread_mutex_unlock(&balance_lock);
    return valid;
}

void add_transaction(Transaction tx) {
    pthread_mutex_lock(&transaction_lock);

    if (pending_transaction_count < TRANSACTIONS_PER_BLOCK) {
        if (validate_transaction(tx)) {
            pending_transactions[pending_transaction_count++] = tx;
            printf("Added transaction: %s -> %s (%.2f)\n", tx.sender, tx.receiver, tx.amount);

            if (pending_transaction_count == TRANSACTIONS_PER_BLOCK) {
                mining = true;
                block_found = false;
                pthread_cond_broadcast(&transaction_cond);
            }
        } else {
            printf("Invalid transaction: %s doesn't have enough funds\n", tx.sender);
        }
    } else {
        printf("Transaction pool is full. Waiting for block to be mined.\n");
    }

    pthread_mutex_unlock(&transaction_lock);
}
void update_balances(Transaction txs[TRANSACTIONS_PER_BLOCK], int miner_id) {
    pthread_mutex_lock(&balance_lock);

    // Update balances from transactions
    for (int i = 0; i < TRANSACTIONS_PER_BLOCK; i++) {
        Transaction tx = txs[i];
        if (strlen(tx.sender) == 0) continue;

        for (int j = 0; j < NUM_NODES; j++) {
            if (strcmp(accounts[j].address, tx.sender) == 0) {
                accounts[j].balance -= tx.amount;
            }
            if (strcmp(accounts[j].address, tx.receiver) == 0) {
                accounts[j].balance += tx.amount;
            }
        }
    }

    // Add mining reward
    if (miner_id >= 0 && miner_id < NUM_NODES) {
        accounts[miner_id].balance += REWARD_AMOUNT;
        network[miner_id].total_rewards += REWARD_AMOUNT;  // Track the reward
        printf("Node %d received mining reward (%.2f)\n", miner_id, REWARD_AMOUNT);
    }

    pthread_mutex_unlock(&balance_lock);
}

// Add this function to display rewards
void print_rewards() {
    printf("\nMining Rewards Summary:\n");
    for (int i = 0; i < NUM_NODES; i++) {
        printf("Node %d received %.2f in mining rewards\n", i, network[i].total_rewards);
    }
}


void add_block_to_chain(Node* node, Block* block, long proof) {
    pthread_mutex_lock(&node->blockchain.lock);

    if (node->blockchain.head == NULL) {
        node->blockchain.head = block;
        node->blockchain.tail = block;
    } else {
        node->blockchain.tail->next = block;
        node->blockchain.tail = block;
    }
    node->blockchain.length++;
    node->blockchain.current_proof = proof;

    pthread_mutex_unlock(&node->blockchain.lock);
}

void broadcast_block(Block* block, long proof, int miner_id) {
    // Update balances only once
    if (block->index > 0) {
        update_balances(block->transactions, miner_id);
    }

    // Create a copy of the block for each node
    for (int i = 0; i < NUM_NODES; i++) {
        Block* block_copy = (Block*)malloc(sizeof(Block));
        memcpy(block_copy, block, sizeof(Block));
        block_copy->next = NULL;
        add_block_to_chain(&network[i], block_copy, proof);
    }
}

void* mine_block(void* arg) {
    Node* node = (Node*)arg;

    while (node->running) {
        pthread_mutex_lock(&transaction_lock);
        while (!mining && node->running) {
            pthread_cond_wait(&transaction_cond, &transaction_lock);
        }

        if (!node->running) {
            pthread_mutex_unlock(&transaction_lock);
            break;
        }

        Transaction current_txs[TRANSACTIONS_PER_BLOCK];
        memcpy(current_txs, pending_transactions, sizeof(Transaction) * TRANSACTIONS_PER_BLOCK);
        pthread_mutex_unlock(&transaction_lock);

        bool found = false;

        pthread_mutex_lock(&mining_lock);
        while (!found && !block_found && node->running) {
            // For malicious nodes (Part 3), sometimes skip mining
            if (node->is_malicious && rand() % 2 == 0) {
                printf("Malicious node %d skipping mining round\n", node->id);
                break;
            }

            // Get the last proof from this node's blockchain
            pthread_mutex_lock(&node->blockchain.lock);
            long last_proof = node->blockchain.current_proof;
            pthread_mutex_unlock(&node->blockchain.lock);

            long proof = calculate_next_proof(last_proof);

            found = true;
            if (!block_found) {
                block_found = true;

                char prev_hash[65];
                if (node->blockchain.tail) {
                    strcpy(prev_hash, node->blockchain.tail->hash);
                } else {
                    strcpy(prev_hash, "0");
                }

                Block* new_block = create_block(node->blockchain.length, prev_hash, current_txs, proof);

                // Malicious nodes might tamper with the block (Part 3)
                if (node->is_malicious && rand() % 2 == 0) {
                    printf("Malicious node %d tampering with block!\n", node->id);
                    new_block->transactions[0].amount *= 2; // Double the first transaction
                }

                printf("\nNode %d mined block %d with proof %ld\n",
                      node->id, new_block->index, proof);

                broadcast_block(new_block, proof, node->id);
                free(new_block);

                // Reset for next block
                pthread_mutex_lock(&transaction_lock);
                pending_transaction_count = 0;
                mining = false;
                pthread_mutex_unlock(&transaction_lock);
            }
        }
        pthread_mutex_unlock(&mining_lock);
    }

    return NULL;
}

void init_network(bool with_malicious, int malicious_count) {
    // Initialize accounts
    for (int i = 0; i < NUM_NODES; i++) {
        sprintf(accounts[i].address, "Node%d", i);
        accounts[i].balance = INITIAL_BALANCE;
    }

    // Create genesis block and initialize nodes
    Block* genesis = create_genesis_block();

    for (int i = 0; i < NUM_NODES; i++) {
        network[i].id = i;
        network[i].running = true;
        network[i].blockchain.head = NULL;
        network[i].blockchain.tail = NULL;
        network[i].blockchain.length = 0;
        network[i].blockchain.current_proof = 0;
        network[i].total_rewards = 0.0;
        network[i].is_malicious = with_malicious && (i < malicious_count); // Set malicious flag
        pthread_mutex_init(&network[i].blockchain.lock, NULL);

        add_block_to_chain(&network[i], genesis, 0);

        pthread_create(&network[i].thread, NULL, mine_block, &network[i]);
    }
    free(genesis);
}

void stop_network() {
    for (int i = 0; i < NUM_NODES; i++) {
        network[i].running = false;
    }

    pthread_cond_broadcast(&transaction_cond);

    for (int i = 0; i < NUM_NODES; i++) {
        pthread_join(network[i].thread, NULL);

        Block* current = network[i].blockchain.head;
        while (current != NULL) {
            Block* next = current->next;
            free(current);
            current = next;
        }
    }
}

void print_blockchain() {
    printf("\nBlockchain:\n");
    for (int i = 0; i < NUM_NODES; i++) {
        printf("Node %d chain (length %d, current proof: %ld):\n",
              i, network[i].blockchain.length, network[i].blockchain.current_proof);
        Block* current = network[i].blockchain.head;
        while (current != NULL) {
            printf("  Block %d [%s]\n", current->index, current->hash);
            for (int j = 0; j < TRANSACTIONS_PER_BLOCK; j++) {
                if (strlen(current->transactions[j].sender) > 0) {
                    printf("    %s -> %s: %.2f\n",
                          current->transactions[j].sender,
                          current->transactions[j].receiver,
                          current->transactions[j].amount);
                }
            }
            current = current->next;
        }
    }
}

void print_balances() {
    printf("\nAccount Balances:\n");
    for (int i = 0; i < NUM_NODES; i++) {
        printf("%s: %.2f\n", accounts[i].address, accounts[i].balance);
    }
}

void test_part1_valid_transactions() {
    printf("\n=== PART 1: TESTING VALID TRANSACTIONS ===\n");

    // Initialize network with no malicious nodes
    init_network(false, 0);

    // Create valid transactions
    Transaction tx1 = {"Node0", "Node1", 10.0, time(NULL)};
    Transaction tx2 = {"Node1", "Node2", 5.0, time(NULL)};
    Transaction tx3 = {"Node2", "Node3", 15.0, time(NULL)};
    Transaction tx4 = {"Node3", "Node4", 8.0, time(NULL)};
    Transaction tx5 = {"Node4", "Node5", 12.0, time(NULL)};
    Transaction tx6 = {"Node5", "Node6", 7.0, time(NULL)};
    Transaction tx7 = {"Node6", "Node5", 10.0, time(NULL)};
    Transaction tx8 = {"Node7", "Node4", 5.0, time(NULL)};
    Transaction tx9 = {"Node1", "Node3", 15.0, time(NULL)};
    printf("Adding transactions...\n");
    add_transaction(tx1);
    add_transaction(tx2);
    add_transaction(tx3);
    sleep(2);

    add_transaction(tx4);
    add_transaction(tx5);
    add_transaction(tx6);
    sleep(2);
    add_transaction(tx7);
    add_transaction(tx8);
    add_transaction(tx9);
    sleep(2);

    // Display blockchain state for each node
    print_blockchain();
    print_balances();
    print_rewards();

    //stop_network();
}

void test_part2_invalid_transactions() {
    printf("\n=== PART 2: TESTING INVALID TRANSACTIONS ===\n");

    // Initialize network with no malicious nodes
    init_network(false, 0);

    // Create both valid and invalid transactions
    Transaction valid_tx = {"Node0", "Node1", 10.0, time(NULL)};
    Transaction invalid_tx1 = {"Node0", "Node1", 200.0, time(NULL)}; // Too much
    Transaction invalid_tx2 = {"NodeX", "Node1", 5.0, time(NULL)};    // Invalid sender

    printf("Adding valid transaction...\n");
    add_transaction(valid_tx);

    printf("\nAttempting invalid transaction (insufficient funds)...\n");
    add_transaction(invalid_tx1);

    printf("\nAttempting invalid transaction (unknown sender)...\n");
    add_transaction(invalid_tx2);

    sleep(2);

    // Display blockchain state - should only show the valid transaction
    print_blockchain();
    print_balances();
    print_rewards();

    //stop_network();
}

void test_part3_malicious_nodes(int malicious_count) {
    printf("\n=== PART 3: TESTING WITH %d MALICIOUS NODES ===\n", malicious_count);

    // Initialize network with specified number of malicious nodes
    init_network(true, malicious_count);

    // Print which nodes are malicious
    printf("Malicious nodes: ");
    for (int i = 0; i < NUM_NODES; i++) {
        if (network[i].is_malicious) {
            printf("%d ", i);
        }
    }
    printf("\n");

    // Create some transactions
    Transaction tx1 = {"Node0", "Node1", 10.0, time(NULL)};
    Transaction tx2 = {"Node1", "Node2", 5.0, time(NULL)};
    Transaction tx3 = {"Node2", "Node3", 15.0, time(NULL)};

    printf("Adding transactions to network with malicious nodes...\n");
    add_transaction(tx1);
    add_transaction(tx2);
    add_transaction(tx3);
    sleep(2);

    // Add more transactions to see behavior
    Transaction tx4 = {"Node3", "Node4", 8.0, time(NULL)};
    Transaction tx5 = {"Node4", "Node5", 12.0, time(NULL)};
    add_transaction(tx4);
    add_transaction(tx5);
    sleep(2);

    // Display results
    print_blockchain();
    print_balances();
    print_rewards();

    //stop_network();
}

int main() {
    srand(time(NULL));

    // Part 1: Test valid transactions
    test_part1_valid_transactions();

    // Part 2: Test invalid transactions
    test_part2_invalid_transactions();

    // Part 3: Test with malicious nodes
    // First with <50% malicious nodes (2 out of 8)
    test_part3_malicious_nodes(2);

    // Then with >50% malicious nodes (5 out of 8)
    test_part3_malicious_nodes(5);

    return 0;
}