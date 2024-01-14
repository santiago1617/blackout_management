#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

// Definiciones de constantes
const float H1_CAPACITY = 15.0;
const float H2_CAPACITY = 5.0;
const float H3_CAPACITY = 2.0;
const float MIN_GENERATION = 100.0;
const float MAX_GENERATION = 150.0;
const float NO_RAIN_INCREMENT = 0.0;
const float AGUACERO_INCREMENT = 2.0; // Incremento para Aguacero
const float DILUVIO_INCREMENT = 4.0;  // Incremento para Diluvio
const int NO_RAIN_DURATION = 0;
const int AGUACERO_DURATION = 10; // Duración de Aguacero
const int DILUVIO_DURATION = 5;   // Duración de Diluvio
volatile sig_atomic_t shutdownRequested = 0;

// Estructura para las centrales hidroeléctricas
typedef struct
{
    char *name;
    float capacity;
    float minWaterLevel;
    float maxWaterLevel;
    float waterLevel;
    int isActive;
    pthread_t thread;
    sem_t sem; // Semáforo para controlar la activación/desactivación
} HydroelectricPlant;

typedef struct HydroelectricPlantNode
{
    HydroelectricPlant *plant;
    struct HydroelectricPlantNode *next;
} HydroelectricPlantNode;

// Variables globales
HydroelectricPlantNode *head = NULL;
pthread_mutex_t listMutex, energyMutex;
sem_t adjustmentSemaphore, sortingSemaphore;
float probA, probB, probC;
float totalEnergyGenerated = 0.0;

// Prototipos de funciones
void *hydroelectricPlantRoutine(void *arg);
void *sortingThreadRoutine(void *arg);
void applyGreedyAlgorithm();
void insertSorted(HydroelectricPlant *plant);
int comparePlants(const HydroelectricPlant *a, const HydroelectricPlant *b);
void sortList();
void findOptimalCombination();
void activatePlant(HydroelectricPlant *plant);
void deactivatePlant(HydroelectricPlant *plant);

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        shutdownRequested = 1;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 7)
    {
        fprintf(stderr, "Uso: %s <Prob A> <Prob B> <Prob C> <Num H1> <Num H2> <Num H3>\n", argv[0]);
        return 1;
    }

    probA = atof(argv[1]);
    probB = atof(argv[2]);
    probC = atof(argv[3]);
    int numH1 = atoi(argv[4]);
    int numH2 = atoi(argv[5]);
    int numH3 = atoi(argv[6]);

    if (probA + probB + probC != 1.0f)
    {
        fprintf(stderr, "Error: la suma de las probabilidades debe ser 1.\n");
        return 1;
    }

    // Configuración del manejador de señales
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Inicializar mutex y semáforos
    pthread_mutex_init(&listMutex, NULL);
    pthread_mutex_init(&energyMutex, NULL);
    sem_init(&adjustmentSemaphore, 0, 0);
    sem_init(&sortingSemaphore, 0, 0);

    // Crear y añadir centrales a la lista
    for (int i = 0; i < numH1; ++i)
    {
        HydroelectricPlant *h1 = malloc(sizeof(HydroelectricPlant));
        *h1 = (HydroelectricPlant){"H1", H1_CAPACITY, 50.0, 200.0, 125.0, 0, PTHREAD_MUTEX_INITIALIZER};
        sem_init(&h1->sem, 0, 0);
        insertSorted(h1);
    }
    for (int i = 0; i < numH2; ++i)
    {
        HydroelectricPlant *h2 = malloc(sizeof(HydroelectricPlant));
        *h2 = (HydroelectricPlant){"H2", H2_CAPACITY, 25.0, 100.0, 62.5, 0, PTHREAD_MUTEX_INITIALIZER};
        sem_init(&h2->sem, 0, 0);
        insertSorted(h2);
    }
    for (int i = 0; i < numH3; ++i)
    {
        HydroelectricPlant *h3 = malloc(sizeof(HydroelectricPlant));
        *h3 = (HydroelectricPlant){"H3", H3_CAPACITY, 10.0, 50.0, 30.0, 0, PTHREAD_MUTEX_INITIALIZER};

        sem_init(&h3->sem, 0, 0);
        insertSorted(h3);
    }
    applyGreedyAlgorithm();

    // Crear y lanzar hilos de centrales
    HydroelectricPlantNode *current = head;
    while (current)
    {
        pthread_create(&current->plant->thread, NULL, hydroelectricPlantRoutine, current->plant);
        current = current->next;
    }

    // Crear y lanzar el hilo de ordenamiento
    pthread_t sortingThread;
    pthread_create(&sortingThread, NULL, sortingThreadRoutine, NULL);

    // Bucle principal
    while (!shutdownRequested)
    {
        sem_wait(&adjustmentSemaphore);
        applyGreedyAlgorithm();
        sem_post(&sortingSemaphore);
    }

    // Esperar a que todos los hilos finalicen
    current = head;
    while (current)
    {
        pthread_join(current->plant->thread, NULL);
        current = current->next;
    }
    pthread_join(sortingThread, NULL);

    // Liberar recursos
    pthread_mutex_destroy(&listMutex);
    sem_destroy(&adjustmentSemaphore);
    sem_destroy(&sortingSemaphore);

    current = head;
    while (current)
    {
        sem_destroy(&current->plant->sem);
        free(current->plant);
        HydroelectricPlantNode *temp = current;
        current = current->next;
        free(temp);
    }

    return 0;
}

void *hydroelectricPlantRoutine(void *arg)
{
    HydroelectricPlant *plant = (HydroelectricPlant *)arg;
    int rainDuration = 0;
    float rainIncrement = 0.0;

    while (!shutdownRequested)
    {
        sem_wait(&plant->sem); // Esperar si la central está desactivada

        if (rainDuration > 0)
        {
            // Incrementar el nivel del agua debido a la lluvia
            plant->waterLevel += rainIncrement;
            rainDuration--;
        }
        else
        {
            // Simular un nuevo evento de lluvia
            float prob = (float)rand() / RAND_MAX;
            if (prob < probA)
            {
                // No lluvia
                rainIncrement = NO_RAIN_INCREMENT;
                rainDuration = NO_RAIN_DURATION;
            }
            else if (prob < probA + probB)
            {
                // Aguacero
                rainIncrement = AGUACERO_INCREMENT;
                rainDuration = AGUACERO_DURATION;
            }
            else
            {
                // Diluvio
                rainIncrement = DILUVIO_INCREMENT;
                rainDuration = DILUVIO_DURATION;
            }
        }

        // Simulación de la operación de la central
        if (plant->isActive)
        {
            pthread_mutex_lock(&energyMutex);
            totalEnergyGenerated += plant->capacity;
            pthread_mutex_unlock(&energyMutex);

            // Reducir el nivel del agua debido a la generación de energía
            plant->waterLevel -= 5.0;

            // Desactivar la central si el nivel de agua está fuera de los límites
            if (plant->waterLevel < plant->minWaterLevel || plant->waterLevel > plant->maxWaterLevel)
            {
                deactivatePlant(plant); // Esto también podría manejar la lógica del semáforo
            }
        }

        sleep(1); // Esperar un segundo antes de la próxima iteración
    }

    // Limpieza y salida ordenada del hilo
    pthread_exit(NULL);
    return NULL;
}
void *sortingThreadRoutine(void *arg)
{
    while (!shutdownRequested)
    {
        // Esperar una señal para comenzar el ordenamiento
        sem_wait(&sortingSemaphore);

        // Bloquear el mutex para asegurar el acceso exclusivo a la lista
        pthread_mutex_lock(&listMutex);

        // Ordenar la lista
        sortList();

        // Desbloquear el mutex después de terminar el ordenamiento
        pthread_mutex_unlock(&listMutex);
    }

    // Limpieza y salida ordenada del hilo
    pthread_exit(NULL);
    return NULL;
}

void deactivatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 0;
    // Aquí no se envía una señal al semáforo; la central se pausará automáticamente en la próxima iteración
}
void activatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 1;
    sem_post(&plant->sem); // "Activar" la central enviando una señal al semáforo
}

void insertSorted(HydroelectricPlant *plant)
{
    HydroelectricPlantNode *newNode = malloc(sizeof(HydroelectricPlantNode));
    newNode->plant = *plant;
    newNode->next = NULL;

    pthread_mutex_lock(&listMutex);

    if (head == NULL || comparePlants(newNode->plant, head->plant) > 0)
    {
        newNode->next = head;
        head = newNode;
    }
    else
    {
        HydroelectricPlantNode *current = head;
        while (current->next != NULL && comparePlants(newNode->plant, current->next->plant) <= 0)
        {
            current = current->next;
        }
        newNode->next = current->next;
        current->next = newNode;
    }

    pthread_mutex_unlock(&listMutex);
}

int comparePlants(HydroelectricPlant a, HydroelectricPlant b)
{
    // Primero comparar por capacidad
    if (a.capacity != b.capacity)
    {
        return (a.capacity > b.capacity) ? -1 : 1;
    }

    // Luego por nivel de agua
    if (a.waterLevel != b.waterLevel)
    {
        return (a.waterLevel > b.waterLevel) ? -1 : 1;
    }

    return 0;
}

void sortList()
{
    if (!head || !head->next)
    {
        return; // No hay nada que ordenar si la lista está vacía o tiene un solo elemento
    }

    HydroelectricPlantNode *sorted = NULL;  // Lista ordenada
    HydroelectricPlantNode *current = head; // Apuntador al elemento actual en la lista original

    while (current != NULL)
    {
        HydroelectricPlantNode *next = current->next; // Guardar el siguiente elemento

        // Insertar el elemento actual en la posición correcta en la lista ordenada
        if (sorted == NULL || comparePlants(current->plant, sorted->plant) > 0)
        {
            current->next = sorted;
            sorted = current;
        }
        else
        {
            HydroelectricPlantNode *sortedCurrent = sorted;
            while (sortedCurrent->next != NULL && comparePlants(current->plant, sortedCurrent->next->plant) <= 0)
            {
                sortedCurrent = sortedCurrent->next;
            }
            current->next = sortedCurrent->next;
            sortedCurrent->next = current;
        }

        current = next; // Mover al siguiente elemento en la lista original
    }

    head = sorted; // Actualizar el inicio de la lista con la lista ordenada
}

void applyGreedyAlgorithm()
{
    pthread_mutex_lock(&listMutex);

    float currentGeneration = 0.0;
    HydroelectricPlantNode *currentNode = head;

    // Desactivar todas las centrales primero
    while (currentNode != NULL)
    {
        deactivatePlant(&currentNode->plant);
        currentNode = currentNode->next;
    }

    currentNode = head;

    // Activar centrales de manera óptima
    while (currentNode != NULL)
    {
        if (currentNode->plant.waterLevel > currentNode->plant.minWaterLevel &&
            currentGeneration + currentNode->plant.capacity <= MAX_GENERATION)
        {
            activatePlant(&currentNode->plant);
            currentGeneration += currentNode->plant.capacity;

            if (currentGeneration >= MIN_GENERATION)
            {
                break; // Detenerse si se alcanza la generación mínima
            }
        }
        currentNode = currentNode->next;
    }

    // Si no se alcanza la generación mínima, buscar una combinación óptima
    if (currentGeneration < MIN_GENERATION || currentGeneration > MAX_GENERATION)
    {
        findOptimalCombination();
    }

    pthread_mutex_unlock(&listMutex);
}

void findOptimalCombination()
{
    pthread_mutex_lock(&listMutex);

    // Desactivar todas las centrales primero
    HydroelectricPlantNode *node = head;
    while (node)
    {
        deactivatePlant(&node->plant);
        node = node->next;
    }

    float bestGeneration = 0.0;
    HydroelectricPlantNode *bestCombinationStart = NULL;

    // Probar diferentes combinaciones
    for (HydroelectricPlantNode *startNode = head; startNode != NULL; startNode = startNode->next)
    {
        node = startNode;
        float currentGeneration = 0.0;

        // Activar centrales secuencialmente desde el nodo de inicio
        while (node != NULL)
        {
            if (node->plant.waterLevel > node->plant.minWaterLevel &&
                currentGeneration + node->plant.capacity <= MAX_GENERATION)
            {
                currentGeneration += node->plant.capacity;

                if (currentGeneration >= MIN_GENERATION && currentGeneration <= MAX_GENERATION)
                {
                    // Si esta combinación es mejor que la mejor encontrada hasta ahora, actualizar
                    if (currentGeneration > bestGeneration)
                    {
                        bestGeneration = currentGeneration;
                        bestCombinationStart = startNode;
                    }
                    break; // Salir del bucle interno si se encuentra una combinación válida
                }
            }
            node = node->next;
        }
    }

    // Aplicar la mejor combinación encontrada
    if (bestCombinationStart != NULL)
    {
        node = head;
        while (node != NULL)
        {
            if (node->plant.waterLevel > node->plant.minWaterLevel &&
                node->plant.waterLevel < node->plant.maxWaterLevel &&
                node->plant.capacity <= MAX_GENERATION - bestGeneration)
            {
                activatePlant(&node->plant);
                bestGeneration += node->plant.capacity;
            }
            else
            {
                deactivatePlant(&node->plant);
            }
            node = node->next;
        }
    }

    pthread_mutex_unlock(&listMutex);
}