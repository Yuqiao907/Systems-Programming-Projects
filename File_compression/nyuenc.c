#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>

#define CHUNK_SIZE 4096
#define MAX_TASKS 300000 // support 1GB file as maximum (1GB / 4KB ≈ 262144 tasks)
#define MAX_FILES 100

// task structure
typedef struct {
    const unsigned char *data;
    size_t size;
    unsigned char *encoded_data;
    size_t encoded_size;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} Task;

// task queue for the whole program
Task **task_queue; // Task **task_queue = Task*[10]; task* -> task
int q_head = 0, q_tail = 0, q_count = 0;
pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_not_empty = PTHREAD_COND_INITIALIZER;
//whether the whole queue is finished
bool done_submitting = false;

// working thread
void* worker_thread(void* arg) {
    (void)arg;
    while (true) {
        pthread_mutex_lock(&q_lock);
        // if the task queue is empty and not done submitting, we wait to assign task/do task
        while (q_count == 0 && !done_submitting) {
            pthread_cond_wait(&q_not_empty, &q_lock);
        }
        // if queue is empty and we done submitting, we quit
        if (q_count == 0 && done_submitting) {
            pthread_mutex_unlock(&q_lock);
            break;
        }
        
        // take one task from the queue
        Task *t = task_queue[q_head];
        q_head = (q_head + 1) % MAX_TASKS;
        q_count--;
        pthread_mutex_unlock(&q_lock);

        // RLE coding
        unsigned char *out = malloc(t->size * 2); // worst case is double size, no repetition of char
        size_t out_idx = 0;
        unsigned char curr = t->data[0];
        int count = 1;

        for (size_t i = 1; i < t->size; i++) {
            if (t->data[i] == curr && count < 255) { // 255 consecutive char as maximum
                count++;
            } else {
                out[out_idx++] = curr;
                out[out_idx++] = count;
                curr = t->data[i];
                count = 1;
            }
        }
        // record the last char
        out[out_idx++] = curr;
        out[out_idx++] = count;

        t->encoded_data = out;
        t->encoded_size = out_idx;

        // task marked as done, wake the main thread, lock to prevent threads take same task
        //lock the thread to change task status
        pthread_mutex_lock(&t->lock);
        t->done = true;
        pthread_cond_signal(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int num_threads = 0;
    int opt;
    
    // Parsing command-line arguments -j
    while ((opt = getopt(argc, argv, "j:")) != -1) {
        if (opt == 'j') {
            num_threads = atoi(optarg);
        }
    }

    if (optind >= argc) return 0; // no input file, just quit

    // ==========================================
    // Q1: single thread scenario, no need to split task into chunks
    if (num_threads == 0) {
        bool has_prev = false;
        unsigned char prev_char = 0;
        int prev_count = 0;

        for (int i = optind; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) continue;
            struct stat sb;
            if (fstat(fd, &sb) < 0 || sb.st_size == 0) { close(fd); continue; }
            
            // make reading faster by mmap
            unsigned char *ptr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);

            for (off_t k = 0; k < sb.st_size; k++) {
                unsigned char c = ptr[k];
                if (has_prev) {
                    if (c == prev_char && prev_count < 255) {
                        prev_count++;
                    } else {
                        fwrite(&prev_char, 1, 1, stdout);
                        fwrite(&prev_count, 1, 1, stdout);
                        prev_char = c;
                        prev_count = 1;
                    }
                } else {
                    prev_char = c;
                    prev_count = 1;
                    has_prev = true;
                }
            }
            munmap(ptr, sb.st_size);
        }
        if (has_prev) {
            fwrite(&prev_char, 1, 1, stdout);
            fwrite(&prev_count, 1, 1, stdout);
        }
        return 0;
    }

    // ==========================================
    // Q2: thread pool run concurrently
    // 6 tasks, 3 theread:
    // create 6 Task, 6 Task* [0x8880 -> Task[1], 0x8881 -> Task[2], 0x8882, 0x8883, ...] : all Tasks
    // a thraed -> Task[3], b thread -> Task[1] 
    // 5 4 6
    task_queue = malloc(MAX_TASKS * sizeof(Task*));
    Task **all_tasks = malloc(MAX_TASKS * sizeof(Task*));
    int total_tasks = 0;

    typedef struct { unsigned char *ptr; size_t size; } MappedFile;
    MappedFile mapped_files[MAX_FILES];
    int num_mapped_files = 0;

    // create working thread pool
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // submitting work：main thread cut file into chunks, put in queue 
    for (int i = optind; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) continue;
        struct stat sb;
        if (fstat(fd, &sb) < 0 || sb.st_size == 0) { close(fd); continue; }

        unsigned char *ptr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        mapped_files[num_mapped_files].ptr = ptr;
        mapped_files[num_mapped_files].size = sb.st_size;
        num_mapped_files++;

        off_t offset = 0;
        while (offset < sb.st_size) {
            size_t chunk = (sb.st_size - offset > (off_t)CHUNK_SIZE) ? CHUNK_SIZE : (sb.st_size - offset);
            Task *t = malloc(sizeof(Task));
            t->data = ptr + offset;
            t->size = chunk;
            t->encoded_data = NULL;
            t->encoded_size = 0;
            t->done = false;
            pthread_mutex_init(&t->lock, NULL);
            pthread_cond_init(&t->cond, NULL);

            all_tasks[total_tasks++] = t;

            // put task into queue
            pthread_mutex_lock(&q_lock);
            task_queue[q_tail] = t;
            q_tail = (q_tail + 1) % MAX_TASKS;
            q_count++;
            pthread_cond_signal(&q_not_empty); // wake a work thread, assign work
            pthread_mutex_unlock(&q_lock);

            offset += chunk;
        }
    }

    // tell all worker threads that all tasks have been submitted, no need for further waiting if have done all assigned work
    pthread_mutex_lock(&q_lock);
    done_submitting = true;
    pthread_cond_broadcast(&q_not_empty);
    pthread_mutex_unlock(&q_lock);

    // merge output to STDOUT
    bool has_prev = false;
    unsigned char prev_char = 0;
    int prev_count = 0;

    for (int i = 0; i < total_tasks; i++) {
        Task *t = all_tasks[i];

        // wait task to complete processing
        pthread_mutex_lock(&t->lock);
        while (!t->done) {
            pthread_cond_wait(&t->cond, &t->lock);
        }
        pthread_mutex_unlock(&t->lock);

        // process the result of the chunk, handle boundaries merging between chunks
        for (size_t j = 0; j < t->encoded_size; j += 2) {
            unsigned char c = t->encoded_data[j];
            unsigned char count = t->encoded_data[j+1];

            if (has_prev) {
                if (c == prev_char) {
                    // the total length of repetiting characters will not exceed 255, so just add up
                    prev_count += count; 
                } else {
                    fwrite(&prev_char, 1, 1, stdout);
                    fwrite(&prev_count, 1, 1, stdout);
                    prev_char = c;
                    prev_count = count;
                }
            } else {
                prev_char = c;
                prev_count = count;
                has_prev = true;
            }
        }
        
        // free all the task resources
        free(t->encoded_data);
        pthread_mutex_destroy(&t->lock);
        pthread_cond_destroy(&t->cond);
        free(t);
    }

    // Write the last char
    if (has_prev) {
        fwrite(&prev_char, 1, 1, stdout);
        fwrite(&prev_count, 1, 1, stdout);
    }

    // ensure threads exit cleanly and release memory
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // unmap files
    for (int i = 0; i < num_mapped_files; i++) {
        munmap(mapped_files[i].ptr, mapped_files[i].size);
    }

    free(task_queue);
    free(all_tasks);
    free(threads);

    return 0;
}