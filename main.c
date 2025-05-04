#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

// ---- Structure de donnees fondamentales ----

// Structure simple pour le hachage
void simple_hash(const char* data, char hash[65]) {
    unsigned long h = 5381;
    int c;
    while ((c = *data++))
        h = ((h << 5) + h) + c;

    snprintf(hash, 65, "%016lx%016lx%016lx%016lx", h, h, h, h);
}

// Transaction generique (peut representer un vote, transfert, etc.)
typedef struct {
    char sender[50];
    char receiver[50];
    double value;
    time_t timestamp;
    char signature[65];
} Transaction;

// Structure d'un bloc
typedef struct Block {
    int index;
    time_t timestamp;
    Transaction transactions[10];
    int transaction_count;
    char previous_hash[65];
    char hash[65];
    int nonce;
    struct Block* next;
} Block;

// Structure de la blockchain
typedef struct {
    Block* head;
    Block* tail;
    int length;
    pthread_mutex_t lock;
} Blockchain;

// Structure d'un noeud du reseau
typedef struct {
    int id;
    Blockchain blockchain;
    pthread_t thread;
    int running;
    bool is_malicious;
    double balance;      // Solde/recompenses du mineur
} Node;

// ---- Variables globales ----
#define MAX_NODES 5
#define MAX_PENDING_TRANSACTIONS 20
#define CONSENSUS_TARGET "000"  // Difficulte de consensus (3 zeros de tete)

Node network[MAX_NODES];
int node_count = 0;

// Transactions en attente
Transaction pending_transactions[MAX_PENDING_TRANSACTIONS];
int pending_transaction_count = 0;
pthread_mutex_t transactions_lock = PTHREAD_MUTEX_INITIALIZER;

// ---- Fonctions de la blockchain ----

// Signer une transaction
void sign_transaction(Transaction* tx) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s%.2f%ld",
             tx->sender, tx->receiver, tx->value, tx->timestamp);
    simple_hash(buffer, tx->signature);
}

// Valider une transaction
bool validate_transaction(Transaction* tx) {
    // Verifier la signature
    char buffer[256];
    char calculated_signature[65];

    snprintf(buffer, sizeof(buffer), "%s%s%.2f%ld",
             tx->sender, tx->receiver, tx->value, tx->timestamp);
    simple_hash(buffer, calculated_signature);

    return strcmp(calculated_signature, tx->signature) == 0;
}

// Miner un bloc (preuve de travail)
void mine_block(Block* block) {
    char buffer[1024];
    bool found = false;

    printf("Mineur %d: Minage du bloc %d en cours...\n",
           block->transactions[0].receiver[4] - '0', block->index);

    while (!found) {
        snprintf(buffer, sizeof(buffer), "%d%ld%s%d",
                 block->index, block->timestamp, block->previous_hash, block->nonce);

        simple_hash(buffer, block->hash);

        // Verifier si le hash commence par le nombre requis de zeros
        if (strncmp(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET)) == 0) {
            found = true;
        } else {
            block->nonce++;

            // Pour eviter une boucle infinie dans la demonstration
            if (block->nonce > 1000) {
                strncpy(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET));
                strcpy(block->hash + strlen(CONSENSUS_TARGET), "demo_hash");
                found = true;
            }
        }
    }

    printf("Bloc %d mine avec nonce: %d | Hash: %.10s...\n",
           block->index, block->nonce, block->hash);
}

// Creer un nouveau bloc
Block* create_block(int index, const char* previous_hash, Transaction* transactions, int count) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    block->index = index;
    block->timestamp = time(NULL);
    block->transaction_count = count;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->nonce = 0;
    block->next = NULL;

    // Copier les transactions
    for (int i = 0; i < count && i < 10; i++) {
        memcpy(&block->transactions[i], &transactions[i], sizeof(Transaction));
    }

    // Miner le bloc pour trouver un hash valide
    mine_block(block);

    return block;
}

// Valider un bloc
bool validate_block(Block* block, const char* expected_previous_hash) {
    // 1. Verifier le hash precedent
    if (strcmp(block->previous_hash, expected_previous_hash) != 0) {
        printf("Hash precedent incorrect\n");
        return false;
    }

    // 2. Verifier le format du hash actuel (consensus)
    if (strncmp(block->hash, CONSENSUS_TARGET, strlen(CONSENSUS_TARGET)) != 0) {
        printf("Hash ne respecte pas le consensus\n");
        return false;
    }

    // 3. Verifier les transactions
    for (int i = 0; i < block->transaction_count; i++) {
        if (!validate_transaction(&block->transactions[i])) {
            printf("Transaction invalide dans le bloc\n");
            return false;
        }
    }

    return true;
}

// Ajouter un bloc a la blockchain
void add_block(Blockchain* blockchain, Block* block) {
    pthread_mutex_lock(&blockchain->lock);

    if (blockchain->head == NULL) {
        // Premier bloc (genesis)
        blockchain->head = block;
        blockchain->tail = block;
        blockchain->length = 1;
    } else {
        // Valider le bloc avant de l'ajouter
        if (validate_block(block, blockchain->tail->hash)) {
            blockchain->tail->next = block;
            blockchain->tail = block;
            blockchain->length++;
            printf("Bloc %d ajoute a la chaine\n", block->index);
        } else {
            printf("Bloc %d rejete (invalide)\n", block->index);
            free(block);
        }
    }

    pthread_mutex_unlock(&blockchain->lock);
}

// Afficher la blockchain
void print_blockchain(const Blockchain* blockchain) {
    Block* current = blockchain->head;

    printf("\n=== BLOCKCHAIN (longueur: %d) ===\n", blockchain->length);

    while (current != NULL) {
        printf("+-----------------------------+\n");
        printf("| BLOC #%-3d    Nonce: %-6d |\n", current->index, current->nonce);
        printf("+-----------------------------+\n");
        printf("| Hash: %.10s...              |\n", current->hash);
        printf("| Prev: %.10s...              |\n", current->previous_hash);
        printf("+-----------------------------+\n");
        printf("| TRANSACTIONS (%d)            |\n", current->transaction_count);

        for (int i = 0; i < current->transaction_count; i++) {
            printf("| %s -> %s: %.1f      |\n",
                  current->transactions[i].sender,
                  current->transactions[i].receiver,
                  current->transactions[i].value);
        }

        printf("+-----------------------------+\n");
        printf("            v\n");

        current = current->next;
    }

    printf("      [FIN DE LA CHAINE]\n");
}

// ---- Fonctions du reseau ----

// Ajouter une transaction au pool
void add_transaction_to_pool(Transaction tx) {
    pthread_mutex_lock(&transactions_lock);

    if (pending_transaction_count < MAX_PENDING_TRANSACTIONS) {
        // Signer la transaction
        sign_transaction(&tx);

        // Ajouter au pool
        memcpy(&pending_transactions[pending_transaction_count], &tx, sizeof(Transaction));
        pending_transaction_count++;
        printf("Transaction ajoutee: %s -> %s: %.1f\n",
               tx.sender, tx.receiver, tx.value);
    }

    pthread_mutex_unlock(&transactions_lock);
}

// Diffuser un bloc aux autres noeuds
void broadcast_block(int sender_id, Block* block) {
    for (int i = 0; i < node_count; i++) {
        if (i != sender_id) {
            // Creer une copie du bloc pour le noeud destinataire
            Block* block_copy = (Block*)malloc(sizeof(Block));
            if (!block_copy) continue;

            // Copier les donnees du bloc
            memcpy(block_copy, block, sizeof(Block));
            block_copy->next = NULL;

            // Ajouter le bloc a la blockchain du noeud destinataire
            add_block(&network[i].blockchain, block_copy);
        }
    }
}

// Traiter les transactions en attente
void process_pending_transactions(Node* node) {
    pthread_mutex_lock(&transactions_lock);

    if (pending_transaction_count > 0) {
        // Prendre jusqu'a 5 transactions
        int count = pending_transaction_count > 5 ? 5 : pending_transaction_count;
        Transaction transactions[10];

        // Copier les transactions a traiter
        memcpy(transactions, pending_transactions, count * sizeof(Transaction));

        // Ajouter une transaction de recompense pour le mineur
        if (count < 10) {
            strcpy(transactions[count].sender, "Systeme");
            sprintf(transactions[count].receiver, "Node%d", node->id);
            transactions[count].value = 10.0;  // Recompense de minage
            transactions[count].timestamp = time(NULL);
            sign_transaction(&transactions[count]);
            count++;
        }

        // Creer un nouveau bloc
        const char* prev_hash = node->blockchain.tail ? node->blockchain.tail->hash : "0";
        Block* new_block = create_block(node->blockchain.length, prev_hash, transactions, count);

        if (new_block) {
            // Ajouter le bloc a la blockchain locale
            add_block(&node->blockchain, new_block);

            // Diffuser aux autres noeuds
            broadcast_block(node->id, new_block);

            // Recompenser le mineur
            node->balance += 10.0;

            // Retirer les transactions traitees du pool
            memmove(pending_transactions,
                    &pending_transactions[count > 5 ? 5 : count],
                    (pending_transaction_count - (count > 5 ? 5 : count)) * sizeof(Transaction));
            pending_transaction_count -= (count > 5 ? 5 : count);
        }
    }

    pthread_mutex_unlock(&transactions_lock);
}

// Fonction principale d'un noeud
void* node_process(void* arg) {
    Node* node = (Node*)arg;
    printf("Noeud %d demarre (malveillant: %s)\n",
           node->id, node->is_malicious ? "oui" : "non");

    while (node->running) {
        // Creer des transactions aleatoires
        if (rand() % 10 == 0) {
            Transaction tx;
            sprintf(tx.sender, "Node%d", node->id);
            sprintf(tx.receiver, "Node%d", rand() % node_count);
            tx.value = (rand() % 10) + 1;
            tx.timestamp = time(NULL);

            add_transaction_to_pool(tx);
        }

        // Traiter les transactions en attente
        process_pending_transactions(node);

        // Attente
        usleep(500000);  // 500ms
    }

    return NULL;
}

// Initialiser un noeud
void init_node(int id, bool is_malicious) {
    if (node_count >= MAX_NODES) return;

    network[node_count].id = id;
    network[node_count].is_malicious = is_malicious;
    network[node_count].blockchain.head = NULL;
    network[node_count].blockchain.tail = NULL;
    network[node_count].blockchain.length = 0;
    network[node_count].running = 1;
    network[node_count].balance = 50.0;  // Solde initial
    pthread_mutex_init(&network[node_count].blockchain.lock, NULL);

    // Creer un bloc genesis
    Transaction genesis_tx = {
        .sender = "Systeme",
        .receiver = "Genesis",
        .value = 100.0,
        .timestamp = time(NULL)
    };
    sign_transaction(&genesis_tx);

    Block* genesis = create_block(0, "0", &genesis_tx, 1);
    add_block(&network[node_count].blockchain, genesis);

    // Demarrer le thread du noeud
    pthread_create(&network[node_count].thread, NULL, node_process, &network[node_count]);
    node_count++;
}

// Arreter tous les noeuds
void stop_nodes() {
    for (int i = 0; i < node_count; i++) {
        network[i].running = 0;
        pthread_join(network[i].thread, NULL);
        pthread_mutex_destroy(&network[i].blockchain.lock);
    }
}

// Afficher les soldes des noeuds
void print_balances() {
    printf("\n=== SOLDES DES MINEURS ===\n");
    for (int i = 0; i < node_count; i++) {
        printf("Noeud %d: %.2f unites\n", i, network[i].balance);
    }
}

// Simuler un vote
void simulate_vote() {
    printf("\n=== SIMULATION D'UN VOTE ===\n");

    // Creer des transactions de vote
    for (int i = 0; i < node_count; i++) {
        Transaction vote;
        sprintf(vote.sender, "Node%d", i);
        strcpy(vote.receiver, "Candidat1");  // Vote pour le candidat 1
        vote.value = 1.0;  // 1 voix
        vote.timestamp = time(NULL);

        add_transaction_to_pool(vote);
    }

    // Laisser le systeme traiter les votes
    printf("Traitement des votes en cours...\n");
    sleep(2);

    // Compter les votes
    int votes_candidat1 = 0;
    int votes_candidat2 = 0;

    for (int n = 0; n < node_count; n++) {
        Block* current = network[n].blockchain.head;
        while (current != NULL) {
            for (int i = 0; i < current->transaction_count; i++) {
                if (strcmp(current->transactions[i].receiver, "Candidat1") == 0) {
                    votes_candidat1++;
                }
                if (strcmp(current->transactions[i].receiver, "Candidat2") == 0) {
                    votes_candidat2++;
                }
            }
            current = current->next;
        }
        break;  // Une seule blockchain suffit pour le decompte
    }

    printf("Resultat du vote:\n");
    printf("- Candidat 1: %d voix\n", votes_candidat1);
    printf("- Candidat 2: %d voix\n", votes_candidat2);
}

// Tenter de modifier un bloc (attaque)
void attempt_block_modification() {
    printf("\n=== TENTATIVE DE MODIFICATION DE BLOC ===\n");

    // Selectionner un noeud malveillant
    int malicious = -1;
    for (int i = 0; i < node_count; i++) {
        if (network[i].is_malicious) {
            malicious = i;
            break;
        }
    }

    if (malicious == -1) {
        printf("Aucun noeud malveillant disponible.\n");
        return;
    }

    // Tenter de modifier une transaction dans un bloc
    Block* target = network[malicious].blockchain.head;
    if (target && target->next) {
        target = target->next;  // Choisir le deuxieme bloc

        printf("Avant modification: %s -> %s: %.1f\n",
               target->transactions[0].sender,
               target->transactions[0].receiver,
               target->transactions[0].value);

        // Modifier la transaction
        strcpy(target->transactions[0].receiver, "Attacker");
        target->transactions[0].value = 999.9;

        printf("Tentative de modification: %s -> %s: %.1f\n",
               target->transactions[0].sender,
               target->transactions[0].receiver,
               target->transactions[0].value);

        // Diffuser le bloc modifie
        printf("Tentative de diffusion du bloc modifie...\n");
        broadcast_block(malicious, target);

        // Verifier si la modification a ete acceptee
        sleep(1);
        int accepted = 0;
        for (int i = 0; i < node_count; i++) {
            if (i != malicious) {
                Block* check = network[i].blockchain.head;
                while (check && check->index != target->index) {
                    check = check->next;
                }

                if (check && strcmp(check->transactions[0].receiver, "Attacker") == 0) {
                    accepted++;
                }
            }
        }

        if (accepted > 0) {
            printf("ALERTE: %d noeuds ont accepte le bloc modifie!\n", accepted);
        } else {
            printf("Tous les noeuds ont rejete le bloc modifie.\n");
        }
    }
}

// ---- Programme principal ----
int main() {
    srand(time(NULL));

    printf("\n======================================\n");
    printf("     SIMULATION DE BLOCKCHAIN        \n");
    printf("======================================\n");

    // Initialiser les noeuds
    printf("\nInitialisation des noeuds...\n");
    init_node(0, false);
    init_node(1, false);
    init_node(2, true);   // Noeud malveillant

    // Laisser le systeme fonctionner un moment
    printf("\nDemarrage de la simulation...\n");
    sleep(3);

    // Simulation de vote
    simulate_vote();

    // Tentative de modification
    attempt_block_modification();

    // Afficher l'etat final
    print_blockchain(&network[0].blockchain);
    print_balances();

    // Arreter la simulation
    printf("\nArret de la simulation...\n");
    stop_nodes();

    return 0;
}