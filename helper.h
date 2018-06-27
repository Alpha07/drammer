/*
 * Copyright 2016, Victor van der Veen
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __HELPER_H__
#define __HELPER_H__

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <map>
#include <sstream>
#include <set>
#include <iostream>
#include <string>
#include <iterator>

#define G(x) (x << 30)
#define M(x) (x << 20)
#define K(x) (x << 10)

#define  B_TO_ORDER(x) (ffs(x / 4096)-1)
#define KB_TO_ORDER(x) (ffs(x / 4)-1)
#define MB_TO_ORDER(x) (ffs(x * 256)-1)

#define ORDER_TO_B(x)  ((1 << x) * 4096)
#define ORDER_TO_KB(x) ((1 << x) * 4)
#define ORDER_TO_MB(x) ((1 << x) / 256)

#define MAX_ORDER 10

#define BILLION 1000000000L
#define MILLION 1000000L

#define FENCING_NONE 0
#define FENCING_ONCE 1
#define FENCING_TWICE 2
#define FENCING_OPTIONS 3

#define MAX_CORES 16

#define CACHELINE_SIZE 64

#include "logger.h"

extern Logger *logger;

static inline uint64_t get_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return BILLION * (uint64_t) t.tv_sec + (uint64_t) t.tv_nsec;
}

static inline uint64_t get_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return MILLION * (uint64_t) tv.tv_sec + tv.tv_usec;
}

static int pagemap_fd = 0;
static bool got_pagemap = true;
    
static inline uintptr_t get_phys_addr(uintptr_t virtual_addr) {
    if (!got_pagemap) return 0;
    if (pagemap_fd == 0) {
        pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
        if (pagemap_fd < 0) {
            got_pagemap = false;
            return 0;
        }
    }
    uint64_t value = 0;
    uint64_t offset;
    offset = (virtual_addr / PAGESIZE) * 8;
    int got = pread(pagemap_fd, &value, sizeof(value), (off_t) offset);

    assert(got == 8);
  
    // Check the "page present" flag.
    if ((value & (1ULL << 63)) == 0) 
        return 0;

    uint64_t frame_num = (value & ((1ULL << 54) - 1));
    return (frame_num * PAGESIZE) | (virtual_addr & (PAGESIZE-1));
}
  
static inline uint64_t compute_median(std::vector<uint64_t> &v) {
    if (v.size() == 0) return 0;
    std::vector<uint64_t> tmp = v;
    size_t n = tmp.size() / 2;
    std::nth_element(tmp.begin(), tmp.begin()+n, tmp.end());
    return tmp[n];
}

static inline int hibit(uint32_t n) {
    n |= (n >>  1);
    n |= (n >>  2);
    n |= (n >>  4);
    n |= (n >>  8);
    n |= (n >> 16);
    return n - (n >> 1);
}


static inline uint64_t get_mem_size() {
  struct sysinfo info;
  sysinfo(&info);
  return (size_t)info.totalram * (size_t)info.mem_unit;
}


static inline int hammer(volatile uint8_t *p1, volatile uint8_t *p2, int count, bool fence, bool cached = false) {
    uint64_t t1, t2;

    t1 = get_ns();
    for (int i = 0; i < count; i++) {
        *p1;
        *p2;
    }
    t2 = get_ns();
    

    return (t2 - t1) / count / 2;
   
}

#define lprint(...) logger->log(__VA_ARGS__)

static inline void dumpfile(const char *filename) {
    std::ifstream f(filename);
    f.clear();
    f.seekg(0, std::ios::beg);
    for (std::string line; getline(f, line); ) {
        if (!line.empty()) lprint("%s\n", line.c_str());
    }
}

static inline void *load(void *info) {
    int ret = 0;
    for (int i = 0; i < 65536; i++) {
        ret += rand();
    }
    return NULL;
}

static inline int getcpus(int *slowest_cpu, int *fastest_cpu) {
    lprint("[CPU] Generating some load to enable all cores\n");
    pthread_t threads[MAX_CORES];
    for (int i = 0; i < MAX_CORES; i++) {
        if(pthread_create(&threads[i], NULL, load, NULL)) {
            perror("Could not create ptread");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < 16; i++) 
        pthread_join(threads[i], NULL);
    
    lprint("[CPU] Looking for core with lowest/highest frequency\n"); 
    std::string cmd = "/system/bin/cat /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq";
    char buffer[256];
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        perror("popen failed");
        return -1;
    }

    int max_freq = 0;
    int min_freq = 0;
    int max_cpu  = 0;
    int min_cpu  = 0;
    int cpu = 0;
    while (!feof(pipe.get())) {
        if (fgets(buffer, 256, pipe.get()) != NULL) {
            int freq = atoi(buffer);
            lprint("[CPU] Max frequency for core %d is %dKHz\n", cpu, freq);
            if (freq > max_freq) {
                max_freq = freq;
                max_cpu = cpu;
            }
            if (min_freq == 0 || freq < min_freq) {
                min_freq = freq;
                min_cpu = cpu;
            }
            cpu++;
        }
    }
    *slowest_cpu = min_cpu;
    *fastest_cpu = max_cpu;
    return cpu;
}

static inline int pincpu(int cpu) {
    lprint("[CPU] Generating some load to enable all cores\n");
    pthread_t threads[MAX_CORES];
    for (int i = 0; i < MAX_CORES; i++) {
        if(pthread_create(&threads[i], NULL, load, NULL)) {
            perror("Could not create ptread");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < 16; i++) 
        pthread_join(threads[i], NULL);

    lprint("[CPU] Pinning to core %d... ", cpu);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset)) {
        perror("Failed to pin cpu");
        return -1;
    } 
    
    lprint("[CPU] Success\n");
    return 0;
}

static inline std::string run(std::string cmd) {
    char buffer[128];
    std::string value = "";
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        perror("popen failed");
        return value;
    }
    while (!feof(pipe.get())) {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            value += buffer;
    }
    return value;
}


static inline std::string getprop(std::string property) {
    std::string cmd = "/system/bin/getprop ";
    cmd += property;

    std::string value = run(cmd);
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}

static inline uint8_t *random_element(std::set<uint8_t *> group) {
    int random_index = rand() % group.size();
    std::set<uint8_t *>::const_iterator it(group.begin());
    std::advance(it, random_index);
    return *it;
}

static inline void unblock_signals(void) {
    int err;
    sigset_t sigset;
    
    err = sigfillset(&sigset);
    if (err != 0) perror("sigfillset");
    
    err = sigprocmask(SIG_UNBLOCK, &sigset, NULL); 
    if (err != 0) perror("sigprocmask");
}    

static std::ifstream meminfo("/proc/meminfo");

static inline size_t read_meminfo(std::string type) {
    meminfo.clear();
    meminfo.seekg(0, std::ios::beg);
    for (std::string line; getline(meminfo, line); ) {
        if (line.find(type) != std::string::npos) {
            std::string kb = line.substr( line.find(':') + 1, line.length() - type.length() - 3 );
            return std::atoi(kb.c_str());
        }
    }
    return 0;
}
static inline size_t get_MemTotal(void) { return read_meminfo("MemTotal"); }
static inline size_t get_MemAvailable(void) { return read_meminfo("MemAvailable"); }
static inline size_t get_MemFree(void) { return read_meminfo("MemFree"); }
static inline size_t get_Buffers(void) { return read_meminfo("Buffers"); }
static inline size_t get_Cached(void) { return read_meminfo("Cached"); }
static inline size_t get_Active(void) { return read_meminfo("Active"); }
static inline size_t get_Inactive(void) { return read_meminfo("Inactive"); }
static inline size_t get_Slab(void) { return read_meminfo("Slab"); }
static inline size_t get_SReclaimable(void) { return read_meminfo("SReclaimable"); }
static inline size_t get_SUnreclaim(void) { return read_meminfo("SUnreclaim"); }

static std::ifstream buddyinfo("/proc/buddyinfo");


template<typename Out>
static inline void split(const std::string &s, char delim, Out result) {
    std::stringstream ss(s);
    std::string item;
    while (getline(ss, item, delim)) {
        if (!item.empty()) *(result++) = item;
    }
}

static inline std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

static inline size_t get_FreeContigMem(size_t minlen) {
    std::map<int,int> chunksOfOrder;

    buddyinfo.clear();
    buddyinfo.seekg(0, std::ios::beg);
    for (std::string line; getline(buddyinfo, line); ) {
//      lprint("line: %s\n", line.c_str());
        if (line.find("Node") != std::string::npos) {

            std::vector<std::string> columns = split(line, ' ');
            int order = 0;
            for (auto col: columns) {
                /* A line looks like:
                 *
                 * Node 0, zone  HighMem    116    304    223     72      0      0      0      0      0      0
                 *
                 * After splitting, we try to convert each column to an integer value and add it to the appropriate order. 
                 * We assume that there are always 4K blocks available. This way, we can find the first column that 
                 * we should parse by simply converting each column and continue until the value != 0
                 */
                int intValue = std::atoi(col.c_str());
                if (intValue == 0 && order == 0) 
                    continue;

                if (chunksOfOrder.count(order) == 0)
                    chunksOfOrder[order] = 0;

                chunksOfOrder[order] += intValue;
//              lprint("chunksOfOrder[%d] = %d\n", order, chunksOfOrder[order]);
                order++;
            }
        }
    }

    size_t available_bytes = 0;
    int minOrder = B_TO_ORDER(minlen);
//  lprint("Looking for any chunks equal or larger than %d bytes == order %d\n", minlen, minOrder);
    for (auto it: chunksOfOrder) {
        int order = it.first;
        int chunks = it.second;

        if (order >= minOrder) {
            available_bytes += ORDER_TO_B(order) * chunks;
        }
//      lprint("available_bytes is now: %zu\n", available_bytes);
    }
    return available_bytes;

}



#endif // __HELPER_H__
