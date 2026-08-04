#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <openssl/sha.h>

#pragma pack(push, 1)
typedef struct BootEntry {
    unsigned char  BS_jmpBoot[3];
    unsigned char  BS_OEMName[8];
    unsigned short BPB_BytsPerSec;
    unsigned char  BPB_SecPerClus;
    unsigned short BPB_RsvdSecCnt;
    unsigned char  BPB_NumFATs;
    unsigned short BPB_RootEntCnt;
    unsigned short BPB_TotSec16;
    unsigned char  BPB_Media;
    unsigned short BPB_FATSz16;
    unsigned short BPB_SecPerTrk;
    unsigned short BPB_NumHeads;
    unsigned int   BPB_HiddSec;
    unsigned int   BPB_TotSec32;
    unsigned int   BPB_FATSz32;
    unsigned short BPB_ExtFlags;
    unsigned short BPB_FSVer;
    unsigned int   BPB_RootClus;
    unsigned short BPB_FSInfo;
    unsigned short BPB_BkBootSec;
    unsigned char  BPB_Reserved[12];
    unsigned char  BS_DrvNum;
    unsigned char  BS_Reserved1;
    unsigned char  BS_BootSig;
    unsigned int   BS_VolID;
    unsigned char  BS_VolLab[11];
    unsigned char  BS_FilSysType[8];
} BootEntry;

typedef struct DirEntry {
    unsigned char  DIR_Name[11];
    unsigned char  DIR_Attr;
    unsigned char  DIR_NTRes;
    unsigned char  DIR_CrtTimeTenth;
    unsigned short DIR_CrtTime;
    unsigned short DIR_CrtDate;
    unsigned short DIR_LstAccDate;
    unsigned short DIR_FstClusHI;
    unsigned short DIR_WrtTime;
    unsigned short DIR_WrtDate;
    unsigned short DIR_FstClusLO;
    unsigned int   DIR_FileSize;
} DirEntry;
#pragma pack(pop)

uint8_t *disk;
BootEntry *bpb;
size_t disk_size;

void print_usage() {
    printf("Usage: ./nyufile disk <options>\n");
    printf("  -i                     Print the file system information.\n");
    printf("  -l                     List the root directory.\n");
    printf("  -r filename [-s sha1]  Recover a contiguous file.\n");
    printf("  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
    exit(1);
}

void format_name(unsigned char *dir_name, char *out_name) {
    int i, j = 0;
    for (i = 0; i < 8 && dir_name[i] != ' '; i++) {
        out_name[j++] = dir_name[i];
    }
    if (dir_name[8] != ' ') {
        out_name[j++] = '.';
        for (i = 8; i < 11 && dir_name[i] != ' '; i++) {
            out_name[j++] = dir_name[i];
        }
    }
    out_name[j] = '\0';
}

void to_fat_name(const char *input, unsigned char *fat_name) {
    memset(fat_name, ' ', 11);
    int i = 0, j = 0;
    while (input[i] != '\0' && input[i] != '.') {
        if (j < 8) fat_name[j++] = input[i];
        i++;
    }
    if (input[i] == '.') {
        i++;
        j = 8;
        while (input[i] != '\0' && j < 11) {
            fat_name[j++] = input[i];
            i++;
        }
    }
}

bool test_clusters(int *selected_clusters, int num_clusters, DirEntry *candidate, unsigned char *expected_hash, uint32_t cluster_size, uint32_t data_offset) {
    unsigned char md[SHA_DIGEST_LENGTH];
    uint32_t file_size = candidate->DIR_FileSize;
    
    if (file_size == 0) {
        SHA1(NULL, 0, md);
        return memcmp(md, expected_hash, SHA_DIGEST_LENGTH) == 0;
    }

    unsigned char *file_buffer = malloc(file_size);
    if (!file_buffer) return false;

    uint32_t bytes_read = 0;
    for (int i = 0; i < num_clusters; i++) {
        uint32_t clus = selected_clusters[i];
        uint32_t offset = data_offset + (clus - 2) * cluster_size;
        uint32_t to_read = (file_size - bytes_read > cluster_size) ? cluster_size : (file_size - bytes_read);
        memcpy(file_buffer + bytes_read, disk + offset, to_read);
        bytes_read += to_read;
    }

    SHA1(file_buffer, file_size, md);
    free(file_buffer);

    return memcmp(md, expected_hash, SHA_DIGEST_LENGTH) == 0;
}

bool find_permutation(int depth, int num_needed, int *selected, int *free_clusters, int num_free, bool *used, DirEntry *candidate, unsigned char *expected_hash, uint32_t cluster_size, uint32_t data_offset) {
    if (depth == num_needed) {
        return test_clusters(selected, num_needed, candidate, expected_hash, cluster_size, data_offset);
    }

    for (int i = 0; i < num_free; i++) {
        if (!used[i]) {
            used[i] = true;
            selected[depth] = free_clusters[i];
            if (find_permutation(depth + 1, num_needed, selected, free_clusters, num_free, used, candidate, expected_hash, cluster_size, data_offset)) {
                return true;
            }
            used[i] = false;
        }
    }
    return false;
}

int main(int argc, char *argv[]) {
    if (argc < 3) print_usage();

    char *disk_img = argv[1];
    bool info_flag = false, list_flag = false, recover_contig = false, recover_noncontig = false;
    char *target_file = NULL, *sha1_hash = NULL;

    int opt;
    while ((opt = getopt(argc - 1, argv + 1, "ilr:R:s:")) != -1) {
        switch (opt) {
            case 'i': info_flag = true; break;
            case 'l': list_flag = true; break;
            case 'r': recover_contig = true; target_file = optarg; break;
            case 'R': recover_noncontig = true; target_file = optarg; break;
            case 's': sha1_hash = optarg; break;
            default: print_usage();
        }
    }

    if (recover_noncontig && !sha1_hash) print_usage();

    int fd = open(disk_img, O_RDWR);
    if (fd == -1) { perror("open"); exit(1); }
    
    struct stat sb;
    if (fstat(fd, &sb) == -1) { perror("fstat"); exit(1); }
    disk_size = sb.st_size;

    disk = mmap(NULL, disk_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) { perror("mmap"); exit(1); }

    bpb = (BootEntry *)disk;

    if (info_flag) {
        printf("Number of FATs = %d\n", bpb->BPB_NumFATs);
        printf("Number of bytes per sector = %d\n", bpb->BPB_BytsPerSec);
        printf("Number of sectors per cluster = %d\n", bpb->BPB_SecPerClus);
        printf("Number of reserved sectors = %d\n", bpb->BPB_RsvdSecCnt);
        return 0;
    }

    uint32_t fat_offset = bpb->BPB_RsvdSecCnt * bpb->BPB_BytsPerSec;
    uint32_t data_offset = fat_offset + (bpb->BPB_NumFATs * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
    uint32_t cluster_size = bpb->BPB_SecPerClus * bpb->BPB_BytsPerSec;
    uint32_t *fat_table = (uint32_t *)(disk + fat_offset);

    if (list_flag) {
        int valid_entries = 0;
        uint32_t current_clus = bpb->BPB_RootClus;
        
        while (current_clus >= 2 && current_clus < 0x0FFFFFF8) {
            uint32_t cluster_offset = data_offset + (current_clus - 2) * cluster_size;
            DirEntry *dir = (DirEntry *)(disk + cluster_offset);
            int entries_per_cluster = cluster_size / sizeof(DirEntry);
            
            for (int i = 0; i < entries_per_cluster; i++) {
                if (dir[i].DIR_Name[0] == 0x00) goto list_done;
                
                if (dir[i].DIR_Name[0] != 0xE5 && dir[i].DIR_Attr != 0x0F) {
                    char filename[13];
                    format_name(dir[i].DIR_Name, filename);
                    uint32_t start_cluster = (dir[i].DIR_FstClusHI << 16) | dir[i].DIR_FstClusLO;
                    
                    if (dir[i].DIR_Attr & 0x10) {
                        printf("%s/ (starting cluster = %u)\n", filename, start_cluster);
                    } else {
                        if (dir[i].DIR_FileSize == 0) {
                            printf("%s (size = 0)\n", filename);
                        } else {
                            printf("%s (size = %u, starting cluster = %u)\n", filename, dir[i].DIR_FileSize, start_cluster);
                        }
                    }
                    valid_entries++;
                }
            }
            current_clus = fat_table[current_clus] & 0x0FFFFFFF;
        }
    list_done:
        printf("Total number of entries = %d\n", valid_entries);
        return 0;
    }

    if (recover_contig || recover_noncontig) {
        unsigned char expected_hash[SHA_DIGEST_LENGTH];
        if (sha1_hash) {
            for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
                sscanf(sha1_hash + 2 * i, "%2hhx", &expected_hash[i]);
            }
        }

        unsigned char target_fat_name[11];
        to_fat_name(target_file, target_fat_name);

        DirEntry *candidate = NULL;
        int name_matches = 0;
        int sha1_matches = 0;
        int final_clusters[20];
        int final_clusters_needed = 0;

        uint32_t current_clus = bpb->BPB_RootClus;
        while (current_clus >= 2 && current_clus < 0x0FFFFFF8) {
            uint32_t cluster_offset = data_offset + (current_clus - 2) * cluster_size;
            DirEntry *dir = (DirEntry *)(disk + cluster_offset);
            int entries_per_cluster = cluster_size / sizeof(DirEntry);

            for (int i = 0; i < entries_per_cluster; i++) {
                if (dir[i].DIR_Name[0] == 0x00) goto search_done;

                if (dir[i].DIR_Name[0] == 0xE5 && dir[i].DIR_Attr != 0x0F) {
                    if (memcmp(dir[i].DIR_Name + 1, target_fat_name + 1, 10) == 0) {
                        name_matches++;
                        uint32_t start_cluster = (dir[i].DIR_FstClusHI << 16) | dir[i].DIR_FstClusLO;
                        int clusters_needed = (dir[i].DIR_FileSize + cluster_size - 1) / cluster_size;

                        if (recover_noncontig && sha1_hash) {
                            if (clusters_needed <= 1) {
                                int sel[1] = {start_cluster};
                                if (test_clusters(sel, clusters_needed, &dir[i], expected_hash, cluster_size, data_offset)) {
                                    candidate = &dir[i];
                                    final_clusters_needed = clusters_needed;
                                    if (clusters_needed == 1) final_clusters[0] = start_cluster;
                                    sha1_matches++;
                                    goto search_done; 
                                }
                            } else if (clusters_needed <= 5) {
                                int free_clusters[20];
                                int num_free = 0;
                                for (int c = 2; c < 22; c++) {
                                    if ((fat_table[c] & 0x0FFFFFFF) == 0 && c != start_cluster) {
                                        free_clusters[num_free++] = c;
                                    }
                                }
                                bool used[20] = {false};
                                int sel[20];
                                sel[0] = start_cluster;
                                if (find_permutation(1, clusters_needed, sel, free_clusters, num_free, used, &dir[i], expected_hash, cluster_size, data_offset)) {
                                    candidate = &dir[i];
                                    final_clusters_needed = clusters_needed;
                                    memcpy(final_clusters, sel, clusters_needed * sizeof(int));
                                    sha1_matches++;
                                    goto search_done;
                                }
                            }
                        } else { // -r (contig)
                            if (sha1_hash) {
                                int sel[20];
                                for (int c = 0; c < clusters_needed; c++) {
                                    sel[c] = start_cluster + c;
                                }
                                if (test_clusters(sel, clusters_needed, &dir[i], expected_hash, cluster_size, data_offset)) {
                                    candidate = &dir[i];
                                    final_clusters_needed = clusters_needed;
                                    memcpy(final_clusters, sel, clusters_needed * sizeof(int));
                                    sha1_matches++;
                                    goto search_done; 
                                }
                            } else {
                                candidate = &dir[i];
                                final_clusters_needed = clusters_needed;
                                for (int c = 0; c < clusters_needed; c++) {
                                    final_clusters[c] = start_cluster + c;
                                }
                            }
                        }
                    }
                }
            }
            current_clus = fat_table[current_clus] & 0x0FFFFFFF;
        }

    search_done:
        if (sha1_hash) {
            if (sha1_matches == 0) {
                printf("%s: file not found\n", target_file);
            } else {
                candidate->DIR_Name[0] = target_file[0];
                if (candidate->DIR_FileSize > 0) {
                    for (int i = 0; i < final_clusters_needed; i++) {
                        uint32_t c = final_clusters[i];
                        uint32_t next_c = (i == final_clusters_needed - 1) ? 0x0FFFFFFF : final_clusters[i + 1];
                        for (int f = 0; f < bpb->BPB_NumFATs; f++) {
                            uint32_t *fat_ptr = (uint32_t *)(disk + fat_offset + f * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
                            fat_ptr[c] = (fat_ptr[c] & 0xF0000000) | (next_c & 0x0FFFFFFF);
                        }
                    }
                }
                printf("%s: successfully recovered with SHA-1\n", target_file);
            }
        } else {
            if (name_matches == 0) {
                printf("%s: file not found\n", target_file);
            } else if (name_matches > 1) {
                printf("%s: multiple candidates found\n", target_file);
            } else {
                candidate->DIR_Name[0] = target_file[0];
                if (candidate->DIR_FileSize > 0) {
                    for (int i = 0; i < final_clusters_needed; i++) {
                        uint32_t c = final_clusters[i];
                        uint32_t next_c = (i == final_clusters_needed - 1) ? 0x0FFFFFFF : final_clusters[i + 1];
                        for (int f = 0; f < bpb->BPB_NumFATs; f++) {
                            uint32_t *fat_ptr = (uint32_t *)(disk + fat_offset + f * bpb->BPB_FATSz32 * bpb->BPB_BytsPerSec);
                            fat_ptr[c] = (fat_ptr[c] & 0xF0000000) | (next_c & 0x0FFFFFFF);
                        }
                    }
                }
                printf("%s: successfully recovered\n", target_file);
            }
        }
    }

    munmap(disk, disk_size);
    close(fd);
    return 0;
}