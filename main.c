#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char sender[50];
    char receiver[50];
    double amount;
    time_t timestamp;
} Transaction;

typedef struct {
    int index;
    time_t timestamp;
    Transaction* transactions;
    int transaction_count;
    char previous_hash[65];
    char hash[65];
} Block;

Transaction create_transaction(const char* sender, const char* receiver, double amount) {
    Transaction t;
    strncpy(t.sender, sender, sizeof(t.sender) - 1);
    strncpy(t.receiver, receiver, sizeof(t.receiver) - 1);
    t.amount = amount;
    t.timestamp = time(NULL);
    return t;
}

Block* create_block(int index, const char* previous_hash, int transaction_capacity) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->transaction_count = 0;

    block->transactions = (Transaction*)malloc(transaction_capacity * sizeof(Transaction));
    if (!block->transactions) {
        free(block);
        return NULL;
    }

    return block;
}

int add_transaction(Block* block, Transaction transaction) {
    block->transactions[block->transaction_count] = transaction;
    block->transaction_count++;
    return block->transaction_count;
}

void print_block(const Block* block) {
    printf("Block #%d\n", block->index);
    printf("Timestamp: %ld\n", block->timestamp);
    printf("Previous Hash: %s\n", block->previous_hash);
    printf("Current Hash: %s\n", block->hash);
    printf("Number of Transactions: %d\n", block->transaction_count);

    printf("Transactions:\n");
    for (int i = 0; i < block->transaction_count; i++) {
        printf("  %s -> %s: %.2f at %ld\n",
               block->transactions[i].sender,
               block->transactions[i].receiver,
               block->transactions[i].amount,
               block->transactions[i].timestamp);
    }
    printf("\n");
}

void free_block(Block* block) {
    if (block) {
        free(block->transactions);
        free(block);
    }
}

int main() {
    Block* blk = create_block(0, "0", 10);

    Transaction t1 = create_transaction("Mia", "Mahdia", 5.0);
    Transaction t2 = create_transaction("Mahdia", "Toubal", 2.5);
    Transaction t3 = create_transaction("Toubal", "Mia", 1.0);

    add_transaction(blk, t1);
    add_transaction(blk, t2);
    add_transaction(blk, t3);

    print_block(blk);

    free_block(blk);

    return 0;
}