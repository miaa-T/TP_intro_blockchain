#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>

// ====================== DEFINITION DES STRUCTURES ======================

// Structure representant une mise a jour de modele dans l'apprentissage federé
typedef struct {
    char client_id[50];         // Identifiant du client qui envoie la mise a jour
    char model_version[50];     // Version du modele
    double accuracy;            // Precision locale obtenue
    int data_samples;           // Nombre d'echantillons utilises
    time_t timestamp;           // Horodatage
    char signature[65];         // Signature pour verifier l'authenticite
} ModelUpdate;

// Structure d'un bloc
typedef struct Block {
    int index;                  // Index du bloc dans la chaine
    time_t timestamp;           // Horodatage de creation du bloc
    ModelUpdate updates[5];     // Mises a jour de modele (max 5 par bloc)
    int update_count;           // Nombre de mises a jour dans ce bloc
    char previous_hash[65];     // Hash du bloc precedent
    char hash[65];              // Hash de ce bloc
    int consensus_solution;     // Solution du probleme de consensus
    int miner_id;               // ID du mineur qui a valide ce bloc
    struct Block* next;         // Pointeur vers le bloc suivant
} Block;

// Structure de la blockchain
typedef struct {
    Block* head;                // Premier bloc
    Block* tail;                // Dernier bloc
    int length;                 // Nombre de blocs
    pthread_mutex_t lock;       // Mutex pour l'acces concurrent
} Blockchain;

// Structure d'un noeud du reseau
typedef struct {
    int id;                     // Identifiant du noeud
    Blockchain blockchain;      // Copie locale de la blockchain
    pthread_t thread;           // Thread du noeud
    int running;                // Flag indiquant si le noeud est actif
    double rewards;             // Recompenses accumulees
    int blocks_mined;           // Nombre de blocs mines
} Node;

// ====================== VARIABLES GLOBALES ======================

#define NUM_NODES 8            // Nombre de noeuds dans le reseau
#define MAX_PENDING_UPDATES 5 // Taille maximale du pool de mises a jour en attente

// Pool de mises a jour en attente
ModelUpdate pending_updates[MAX_PENDING_UPDATES];
int pending_update_count = 0;
pthread_mutex_t updates_lock = PTHREAD_MUTEX_INITIALIZER;

// Reseau de noeuds
Node network[NUM_NODES];
int global_consensus_difficulty = 0; // Difficulte initiale pour le consensus

// Mutex pour la synchronisation de l'acces aux structures partagees
pthread_mutex_t consensus_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t console_lock = PTHREAD_MUTEX_INITIALIZER;

// ====================== FONCTIONS UTILITAIRES ======================

// Fonction de hachage simplifiee
void simple_hash(const char* data, char hash[65]) {
    unsigned long h = 5381;
    int c;
    while ((c = *data++))
        h = ((h << 5) + h) + c;

    snprintf(hash, 65, "%016lx%016lx%016lx%016lx", h, h, h, h);
}

// Affichage synchronise pour eviter les melanges de sortie
void safe_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&console_lock);
    vprintf(format, args);
    fflush(stdout);
    pthread_mutex_unlock(&console_lock);

    va_end(args);
}

// ====================== FONCTIONS DE GESTION DES MISES A JOUR ======================

// Signer une mise a jour de modele
void sign_model_update(ModelUpdate* update) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s%s%.2f%d%ld",
             update->client_id, update->model_version,
             update->accuracy, update->data_samples,
             update->timestamp);
    simple_hash(buffer, update->signature);
}

// Valider une mise a jour de modele
bool validate_model_update(ModelUpdate* update) {
    // Verifier la signature
    char buffer[256];
    char calculated_signature[65];

    snprintf(buffer, sizeof(buffer), "%s%s%.2f%d%ld",
             update->client_id, update->model_version,
             update->accuracy, update->data_samples,
             update->timestamp);
    simple_hash(buffer, calculated_signature);

    // Verifier que la signature correspond
    if (strcmp(calculated_signature, update->signature) != 0) {
        return false;
    }

    // Verifier que la precision est dans un intervalle raisonnable
    if (update->accuracy < 0.0 || update->accuracy > 1.0) {
        return false;
    }

    // Verifier que le nombre d'echantillons est positif
    return update->data_samples > 0;
}

// Ajouter une mise a jour au pool
void add_update_to_pool(ModelUpdate update) {
    pthread_mutex_lock(&updates_lock);

    if (pending_update_count < MAX_PENDING_UPDATES) {
        // Signer la mise a jour
        sign_model_update(&update);

        // Ajouter au pool si valide
        if (validate_model_update(&update)) {
            memcpy(&pending_updates[pending_update_count], &update, sizeof(ModelUpdate));
            pending_update_count++;
            safe_printf("Nouvelle mise a jour ajoutee au pool: %s (precision: %.2f)\n",
                   update.client_id, update.accuracy);
        } else {
            safe_printf("Mise a jour invalide rejetee: %s\n", update.client_id);
        }
    }

    pthread_mutex_unlock(&updates_lock);
}

// ====================== FONCTIONS DE CONSENSUS ET MINING ======================

// Trouver le prochain nombre impair divisible par 3 au-dessus d'une valeur
int find_next_odd_divisible_by_3(int start_value) {
    int num = start_value;
    // S'assurer que nous commencons par un nombre impair
    if (num % 2 == 0) num++;

    // Trouver le prochain nombre impair divisible par 3
    while (num % 3 != 0 || num <= start_value) {
        num += 2; // Passer au prochain nombre impair
    }

    return num;
}

// Resoudre le probleme de consensus pour un bloc
int solve_consensus(Block* block, int miner_id) {
    // Le probleme: trouver le prochain nombre impair divisible par 3
    // au-dessus de la difficulte actuelle
    int solution = find_next_odd_divisible_by_3(global_consensus_difficulty + block->index);

    // Simuler le travail de minage
    int work_time = rand() % 100 + 50;  // 50-150ms de travail simule
    usleep(work_time * 1000);

    safe_printf("Mineur %d: Calcul solution consensus pour bloc %d: %d\n",
           miner_id, block->index, solution);

    return solution;
}

// Creer un hash pour un bloc
void hash_block(Block* block) {
    char buffer[1024];

    // Concatener les donnees du bloc
    snprintf(buffer, sizeof(buffer), "%d%ld%s%d%d",
             block->index, block->timestamp, block->previous_hash,
             block->update_count, block->consensus_solution);

    // Ajouter les donnees des mises a jour
    for (int i = 0; i < block->update_count; i++) {
        char update_data[256];
        snprintf(update_data, sizeof(update_data), "%s%s%.2f%d",
                 block->updates[i].client_id,
                 block->updates[i].model_version,
                 block->updates[i].accuracy,
                 block->updates[i].data_samples);
        strcat(buffer, update_data);
    }

    // Calculer le hash
    simple_hash(buffer, block->hash);
}

// Valider un bloc
bool validate_block(Block* block, const char* expected_previous_hash) {
    // 1. Verifier le hash precedent
    if (strcmp(block->previous_hash, expected_previous_hash) != 0) {
        safe_printf("Hash precedent incorrect pour bloc %d\n", block->index);
        return false;
    }

    // 2. Verifier la solution du consensus
    int expected_solution = find_next_odd_divisible_by_3(global_consensus_difficulty + block->index);
    if (block->consensus_solution != expected_solution) {
        safe_printf("Solution de consensus incorrecte pour bloc %d\n", block->index);
        return false;
    }

    // 3. Verifier les mises a jour
    for (int i = 0; i < block->update_count; i++) {
        if (!validate_model_update(&block->updates[i])) {
            safe_printf("Mise a jour invalide dans le bloc %d\n", block->index);
            return false;
        }
    }

    // 4. Recalculer et verifier le hash
    char calculated_hash[65];
    char buffer[1024];

    snprintf(buffer, sizeof(buffer), "%d%ld%s%d%d",
             block->index, block->timestamp, block->previous_hash,
             block->update_count, block->consensus_solution);

    for (int i = 0; i < block->update_count; i++) {
        char update_data[256];
        snprintf(update_data, sizeof(update_data), "%s%s%.2f%d",
                 block->updates[i].client_id,
                 block->updates[i].model_version,
                 block->updates[i].accuracy,
                 block->updates[i].data_samples);
        strcat(buffer, update_data);
    }

    simple_hash(buffer, calculated_hash);

    if (strcmp(calculated_hash, block->hash) != 0) {
        safe_printf("Hash de bloc incorrect pour bloc %d\n", block->index);
        return false;
    }

    return true;
}

// ====================== FONCTIONS DE BLOCKCHAIN ======================

// Creer un nouveau bloc
Block* create_block(int index, const char* previous_hash, ModelUpdate* updates,
                   int count, int miner_id) {
    Block* block = (Block*)malloc(sizeof(Block));
    if (!block) return NULL;

    // Initialiser les champs du bloc
    block->index = index;
    block->timestamp = time(NULL);
    block->update_count = count;
    block->miner_id = miner_id;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
    block->next = NULL;

    // Copier les mises a jour
    for (int i = 0; i < count && i < 5; i++) {
        memcpy(&block->updates[i], &updates[i], sizeof(ModelUpdate));
    }

    // Resoudre le probleme de consensus
    block->consensus_solution = solve_consensus(block, miner_id);

    // Calculer le hash du bloc
    hash_block(block);

    return block;
}

// Ajouter un bloc a la blockchain
bool add_block(Blockchain* blockchain, Block* block) {
    pthread_mutex_lock(&blockchain->lock);
    bool result = false;

    if (blockchain->head == NULL) {
        // Premier bloc (genesis)
        blockchain->head = block;
        blockchain->tail = block;
        blockchain->length = 1;
        result = true;
    } else {
        // Valider le bloc avant de l'ajouter
        if (validate_block(block, blockchain->tail->hash)) {
            blockchain->tail->next = block;
            blockchain->tail = block;
            blockchain->length++;
            result = true;
        } else {
            free(block);
            result = false;
        }
    }

    pthread_mutex_unlock(&blockchain->lock);
    return result;
}

// Afficher la blockchain
void print_blockchain(const Blockchain* blockchain) {
    Block* current = blockchain->head;

    safe_printf("\n=== BLOCKCHAIN (longueur: %d) ===\n", blockchain->length);

    while (current != NULL) {
        safe_printf("-------------------------------------------\n");
        safe_printf("| BLOC #%-3d   Mine par: Noeud %-2d        |\n",
               current->index, current->miner_id);
        safe_printf("-------------------------------------------\n");
        safe_printf("| Hash: %.10s...                          |\n", current->hash);
        safe_printf("| Solution consensus: %-3d                 |\n", current->consensus_solution);
        safe_printf("-------------------------------------------\n");
        safe_printf("| MISES A JOUR DE MODELE (%d)              |\n", current->update_count);

        for (int i = 0; i < current->update_count; i++) {
            safe_printf("| %s: %.2f (echantillons: %d)       |\n",
                   current->updates[i].client_id,
                   current->updates[i].accuracy,
                   current->updates[i].data_samples);
        }

        safe_printf("-------------------------------------------\n");
        safe_printf("                   v\n");

        current = current->next;
    }

    safe_printf("            [FIN DE LA CHAINE]\n");
}

// ====================== FONCTIONS DE RESEAU ET DIFFUSION ======================

// Diffuser un bloc aux autres noeuds
void broadcast_block(int sender_id, Block* original_block) {
    pthread_mutex_lock(&consensus_lock);

    // Diffuser le bloc a tous les autres noeuds
    for (int i = 0; i < NUM_NODES; i++) {
        if (i != sender_id && network[i].running) {
            // Creer une copie du bloc pour le noeud destinataire
            Block* block_copy = (Block*)malloc(sizeof(Block));
            if (!block_copy) continue;

            // Copier les donnees du bloc
            memcpy(block_copy, original_block, sizeof(Block));
            block_copy->next = NULL;

            // Tenter d'ajouter le bloc a la blockchain du noeud destinataire
            if (add_block(&network[i].blockchain, block_copy)) {
                safe_printf("Bloc %d propage de noeud %d vers noeud %d\n",
                       original_block->index, sender_id, i);
            } else {
                safe_printf("Bloc %d rejete par noeud %d\n", original_block->index, i);
            }
        }
    }

    pthread_mutex_unlock(&consensus_lock);
}

// Traiter les mises a jour en attente
void process_pending_updates(Node* node) {
    pthread_mutex_lock(&updates_lock);

    if (pending_update_count > 0) {
        // Ne prendre que jusqu'a 5 mises a jour
        int count = pending_update_count > 5 ? 5 : pending_update_count;
        ModelUpdate updates_to_process[5];

        // Copier les mises a jour a traiter
        memcpy(updates_to_process, pending_updates, count * sizeof(ModelUpdate));

        // Creer un nouveau bloc avec ces mises a jour
        const char* prev_hash = node->blockchain.tail ? node->blockchain.tail->hash : "0";
        int next_index = node->blockchain.length;

        pthread_mutex_unlock(&updates_lock);

        // Creer et miner le bloc
        Block* new_block = create_block(next_index, prev_hash, updates_to_process, count, node->id);

        pthread_mutex_lock(&consensus_lock);

        // Verifier si quelqu'un d'autre a deja mine un bloc avec cet index
        if (node->blockchain.length == next_index) {
            // Personne n'a encore mine ce bloc, nous sommes les premiers!
            if (add_block(&node->blockchain, new_block)) {
                safe_printf("Noeud %d a mine le bloc %d avec succes\n", node->id, next_index);

                // Recompense pour le mineur qui a resolu le bloc en premier
                node->rewards += 10.0;
                node->blocks_mined++;

                // Diffuser aux autres noeuds
                broadcast_block(node->id, new_block);

                // Mettre a jour le pool de mises a jour
                pthread_mutex_lock(&updates_lock);
                if (pending_update_count >= count) {
                    memmove(pending_updates,
                            &pending_updates[count],
                            (pending_update_count - count) * sizeof(ModelUpdate));
                    pending_update_count -= count;
                }
                pthread_mutex_unlock(&updates_lock);

                // Augmenter la difficulte periodiquement
                if (next_index % 5 == 0) {
                    global_consensus_difficulty += 3;
                    safe_printf("Difficulte de consensus augmentee a %d\n", global_consensus_difficulty);
                }
            }
        } else {
            // Quelqu'un nous a devance, liberer la memoire
            safe_printf("Noeud %d: trop tard pour le bloc %d, un autre mineur a ete plus rapide\n",
                   node->id, next_index);
            free(new_block);
        }

        pthread_mutex_unlock(&consensus_lock);
    } else {
        pthread_mutex_unlock(&updates_lock);
    }
}

// Fonction principale d'un noeud
void* node_process(void* arg) {
    Node* node = (Node*)arg;
    safe_printf("Noeud %d demarre\n", node->id);

    while (node->running) {
        // Generer aleatoirement des mises a jour de modele
        if (rand() % 20 == 0) {
            ModelUpdate update;
            sprintf(update.client_id, "Client%d", rand() % 100);
            sprintf(update.model_version, "v1.%d", rand() % 10);
            update.accuracy = (rand() % 100) / 100.0;  // Entre 0 et 0.99
            update.data_samples = 1000 + (rand() % 9000);  // Entre 1000 et 10000
            update.timestamp = time(NULL);

            add_update_to_pool(update);
        }

        // Tenter de traiter les mises a jour en attente
        process_pending_updates(node);

        // Attente aleatoire pour simuler des vitesses de traitement differentes
        usleep((rand() % 200 + 50) * 1000);  // 50-250ms
    }

    safe_printf("Noeud %d arrete\n", node->id);
    return NULL;
}

// ====================== INITIALISATION ET GESTION DU RESEAU ======================

// Initialiser un noeud
void init_node(int id) {
    if (id >= NUM_NODES) return;

    network[id].id = id;
    network[id].blockchain.head = NULL;
    network[id].blockchain.tail = NULL;
    network[id].blockchain.length = 0;
    network[id].running = 1;
    network[id].rewards = 0.0;
    network[id].blocks_mined = 0;
    pthread_mutex_init(&network[id].blockchain.lock, NULL);

    // Creer un bloc genesis uniquement pour le noeud 0
    if (id == 0) {
        ModelUpdate genesis_update = {
            .client_id = "Systeme",
            .model_version = "v1.0",
            .accuracy = 0.5,
            .data_samples = 1000,
            .timestamp = time(NULL)
        };
        sign_model_update(&genesis_update);

        Block* genesis = create_block(0, "0", &genesis_update, 1, 0);
        add_block(&network[id].blockchain, genesis);

        // Diffuser le bloc genesis aux autres noeuds
        for (int i = 1; i < NUM_NODES; i++) {
            Block* genesis_copy = (Block*)malloc(sizeof(Block));
            if (genesis_copy) {
                memcpy(genesis_copy, genesis, sizeof(Block));
                genesis_copy->next = NULL;
                add_block(&network[i].blockchain, genesis_copy);
            }
        }
    }

    // Demarrer le thread du noeud
    pthread_create(&network[id].thread, NULL, node_process, &network[id]);
}

// Arreter tous les noeuds
void stop_nodes() {
    for (int i = 0; i < NUM_NODES; i++) {
        network[i].running = 0;
        pthread_join(network[i].thread, NULL);
        pthread_mutex_destroy(&network[i].blockchain.lock);
    }
}

// Afficher les recompenses et statistiques des noeuds
void print_node_stats() {
    safe_printf("\n=== STATISTIQUES DES NOEUDS ===\n");
    safe_printf("--------------------------------\n");
    safe_printf("| Noeud   | Blocs mines | Recompenses   |\n");
    safe_printf("--------------------------------\n");

    for (int i = 0; i < NUM_NODES; i++) {
        safe_printf("| %2d     | %10d  | %13.2f |\n",
               i, network[i].blocks_mined, network[i].rewards);
    }

    safe_printf("--------------------------------\n");
}

// ====================== FONCTIONS DE SIMULATION ======================

// Simuler une attaque (tentative de modification)
void simulate_attack() {
    safe_printf("\n=== SIMULATION D'UNE TENTATIVE D'ATTAQUE ===\n");

    // Choisir un noeud malveillant (noeud 7 dans cet exemple)
    int malicious_node = 7;

    // Trouver un bloc a modifier (deuxieme bloc)
    Block* target = network[malicious_node].blockchain.head;
    if (target) target = target->next;  // Cibler le deuxieme bloc s'il existe

    if (target) {
        safe_printf("Noeud %d tente de modifier le bloc %d\n", malicious_node, target->index);

        // Avant modification
        safe_printf("Avant: Client %s, precision %.2f\n",
               target->updates[0].client_id, target->updates[0].accuracy);

        // Modifier frauduleusement les donnees
        strcpy(target->updates[0].client_id, "AttackerClient");
        target->updates[0].accuracy = 0.99;  // Falsifier les resultats

        safe_printf("Apres: Client %s, precision %.2f\n",
               target->updates[0].client_id, target->updates[0].accuracy);

        // Tenter de diffuser le bloc modifie
        safe_printf("Tentative de propagation du bloc modifie...\n");

        // Simuler un delai
        sleep(1);

        // Verifier si la modification a ete acceptee
        bool accepted = false;
        for (int i = 0; i < NUM_NODES; i++) {
            if (i != malicious_node) {
                Block* check = network[i].blockchain.head;
                int count = 0;
                while (check && count < target->index) {
                    check = check->next;
                    count++;
                }

                if (check && strcmp(check->updates[0].client_id, "AttackerClient") == 0) {
                    accepted = true;
                    break;
                }
            }
        }

        if (accepted) {
            safe_printf("ALERTE: La modification frauduleuse a ete acceptee!\n");
        } else {
            safe_printf("Modification rejetee: le hash ne correspond plus au contenu\n");
        }
    } else {
        safe_printf("Pas assez de blocs pour simuler une attaque\n");
    }
}

// Simuler l'apprentissage federé
void simulate_federated_learning() {
    safe_printf("\n=== SIMULATION D'APPRENTISSAGE FEDERE ===\n");

    // Simuler 10 clients qui envoient des mises a jour de modele
    for (int i = 0; i < 10; i++) {
        ModelUpdate update;
        sprintf(update.client_id, "FedClient%d", i);
        sprintf(update.model_version, "v2.0");

        // Simuler une amelioration progressive du modele
        update.accuracy = 0.7 + (i * 0.02);  // 0.7 a 0.9
        update.data_samples = 500 + (i * 100);  // 500 a 1400
        update.timestamp = time(NULL);

        add_update_to_pool(update);

        // Petite pause entre les ajouts
        usleep(100000);  // 100ms
    }

    safe_printf("10 mises a jour de modele ajoutees au pool\n");
    safe_printf("Attente du traitement par les mineurs...\n");

    // Attendre que les mineurs traitent les mises a jour
    sleep(3);

    // Analyser les resultats
    safe_printf("\n=== RESULTATS D'APPRENTISSAGE FEDERE ===\n");

    // Calculer la precision moyenne du modele agrege
    double total_accuracy = 0.0;
    int count = 0;

    Block* current = network[0].blockchain.head;
    while (current != NULL) {
        for (int i = 0; i < current->update_count; i++) {
            total_accuracy += current->updates[i].accuracy;
            count++;
        }
        current = current->next;
    }

    if (count > 0) {
        double avg_accuracy = total_accuracy / count;
        safe_printf("Precision moyenne du modele agrege: %.4f\n", avg_accuracy);
    }
}

// ====================== PROGRAMME PRINCIPAL ======================

int main() {
    srand(time(NULL));

    safe_printf("\n---------------------------------------------\n");
    safe_printf("| SIMULATION DE BLOCKCHAIN POUR L'APPRENTISSAGE FEDERE |\n");
    safe_printf("---------------------------------------------\n");

    // Initialiser les noeuds
    safe_printf("\nInitialisation du reseau avec %d noeuds...\n", NUM_NODES);
    for (int i = 0; i < NUM_NODES; i++) {
        init_node(i);
        safe_printf("   Noeud %d initialise\n", i);
    }

    // Laisser le systeme fonctionner un moment
    safe_printf("\nDemarrage de la simulation pour 5 secondes...\n");
    sleep(5);

    // Simuler l'apprentissage federé
    simulate_federated_learning();

    // Attendre que les blocs soient traites
    safe_printf("\nTraitement des blocs pendant 3 secondes...\n");
    sleep(3);

    // Simuler une tentative d'attaque
    simulate_attack();

    // Afficher l'etat final de la blockchain
    safe_printf("\nEtat final de la blockchain du noeud 0:\n");
    print_blockchain(&network[0].blockchain);

    // Afficher les statistiques des noeuds
    print_node_stats();

    // Arreter la simulation
    safe_printf("\nArret de la simulation...\n");
    stop_nodes();

    pthread_mutex_destroy(&updates_lock);
    pthread_mutex_destroy(&consensus_lock);
    pthread_mutex_destroy(&console_lock);

    return 0;
}