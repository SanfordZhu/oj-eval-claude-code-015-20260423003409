#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

const char* INDEX_FILE = "index.dat";
const char* DATA_FILE = "data.dat";
const int MAX_INDEX_LEN = 64;

struct IndexEntry {
    char index[MAX_INDEX_LEN];
    uint64_t first_node;
};

struct DataNode {
    int32_t value;
    uint64_t next;
    uint8_t deleted;
};

struct HashEntry {
    uint32_t hash;
    int32_t pos;
};

class FileDB {
private:
    int index_fd;
    int data_fd;
    uint8_t* index_map;
    uint8_t* data_map;
    size_t index_size;
    size_t data_size;
    uint64_t next_node_id;

    std::vector<HashEntry> hash_table;
    static const size_t HASH_SIZE = 180000;
    static const uint32_t EMPTY_HASH = 0;

    uint32_t hash_string(const std::string& s) {
        uint32_t h = 2166136261u;
        for (char c : s) {
            h ^= static_cast<uint32_t>(c);
            h *= 16777619u;
        }
        return h == 0 ? 1 : h;
    }

    int32_t hash_find(const std::string& index) {
        uint32_t h = hash_string(index);
        size_t pos = h % HASH_SIZE;

        for (size_t i = 0; i < HASH_SIZE; i++) {
            size_t idx = (pos + i) % HASH_SIZE;
            if (hash_table[idx].hash == EMPTY_HASH) {
                return -1;
            }
            if (hash_table[idx].hash == h) {
                IndexEntry* entry = get_index_entry(hash_table[idx].pos);
                if (strcmp(entry->index, index.c_str()) == 0) {
                    return hash_table[idx].pos;
                }
            }
        }
        return -1;
    }

    void hash_insert(const std::string& index, int32_t pos) {
        uint32_t h = hash_string(index);
        size_t pos_idx = h % HASH_SIZE;

        for (size_t i = 0; i < HASH_SIZE; i++) {
            size_t idx = (pos_idx + i) % HASH_SIZE;
            if (hash_table[idx].hash == EMPTY_HASH) {
                hash_table[idx].hash = h;
                hash_table[idx].pos = pos;
                return;
            }
        }
    }

    void hash_delete(const std::string& index) {
        uint32_t h = hash_string(index);
        size_t pos_idx = h % HASH_SIZE;

        for (size_t i = 0; i < HASH_SIZE; i++) {
            size_t idx = (pos_idx + i) % HASH_SIZE;
            if (hash_table[idx].hash == EMPTY_HASH) {
                return;
            }
            if (hash_table[idx].hash == h) {
                IndexEntry* entry = get_index_entry(hash_table[idx].pos);
                if (strcmp(entry->index, index.c_str()) == 0) {
                    hash_table[idx].hash = EMPTY_HASH;
                    return;
                }
            }
        }
    }

    IndexEntry* get_index_entry(int32_t pos) {
        return reinterpret_cast<IndexEntry*>(index_map + sizeof(int32_t) + pos * sizeof(IndexEntry));
    }

    DataNode* get_data_node(uint64_t node_id) {
        return reinterpret_cast<DataNode*>(data_map + sizeof(uint64_t) + (node_id - 1) * sizeof(DataNode));
    }

    void init_files() {
        index_fd = open(INDEX_FILE, O_RDWR | O_CREAT, 0644);
        if (index_fd < 0) {
            std::cerr << "Failed to open index file" << std::endl;
            exit(1);
        }

        struct stat st;
        if (fstat(index_fd, &st) == 0 && st.st_size > 0) {
            index_size = st.st_size;
        } else {
            int32_t count = 0;
            write(index_fd, &count, sizeof(count));
            index_size = sizeof(int32_t);
        }

        index_map = reinterpret_cast<uint8_t*>(mmap(nullptr, index_size, PROT_READ | PROT_WRITE, MAP_SHARED, index_fd, 0));
        if (index_map == MAP_FAILED) {
            close(index_fd);
            std::cerr << "Failed to mmap index file" << std::endl;
            exit(1);
        }

        data_fd = open(DATA_FILE, O_RDWR | O_CREAT, 0644);
        if (data_fd < 0) {
            std::cerr << "Failed to open data file" << std::endl;
            exit(1);
        }

        if (fstat(data_fd, &st) == 0 && st.st_size > 0) {
            data_size = st.st_size;
        } else {
            next_node_id = 1;
            write(data_fd, &next_node_id, sizeof(next_node_id));
            data_size = sizeof(uint64_t);
        }

        data_map = reinterpret_cast<uint8_t*>(mmap(nullptr, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0));
        if (data_map == MAP_FAILED) {
            close(data_fd);
            std::cerr << "Failed to mmap data file" << std::endl;
            exit(1);
        }

        next_node_id = *reinterpret_cast<uint64_t*>(data_map);
        if (data_map == MAP_FAILED) {
            close(data_fd);
            std::cerr << "Failed to mmap data file" << std::endl;
            exit(1);
        }

        hash_table.resize(HASH_SIZE);
        for (auto& entry : hash_table) {
            entry.hash = EMPTY_HASH;
        }

        rebuild_hash_table();
    }

    void rebuild_hash_table() {
        int32_t count = *reinterpret_cast<int32_t*>(index_map);
        for (int32_t i = 0; i < count; i++) {
            IndexEntry* entry = get_index_entry(i);
            hash_insert(entry->index, i);
        }
    }

    int32_t get_index_count() {
        return *reinterpret_cast<int32_t*>(index_map);
    }

    void set_index_count(int32_t count) {
        *reinterpret_cast<int32_t*>(index_map) = count;
    }

    void save_next_node_id() {
        *reinterpret_cast<uint64_t*>(data_map) = next_node_id;
    }

    void ensure_data_size(size_t required_size) {
        if (required_size > data_size) {
            munmap(data_map, data_size);
            data_size = required_size;
            ftruncate(data_fd, data_size);
            data_map = reinterpret_cast<uint8_t*>(mmap(nullptr, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, data_fd, 0));
            if (data_map == MAP_FAILED) {
                close(data_fd);
                std::cerr << "Failed to remap data file" << std::endl;
                exit(1);
            }
        }
    }

    void ensure_index_size(size_t required_size) {
        if (required_size > index_size) {
            munmap(index_map, index_size);
            index_size = required_size;
            ftruncate(index_fd, index_size);
            index_map = reinterpret_cast<uint8_t*>(mmap(nullptr, index_size, PROT_READ | PROT_WRITE, MAP_SHARED, index_fd, 0));
            if (index_map == MAP_FAILED) {
                close(index_fd);
                std::cerr << "Failed to remap index file" << std::endl;
                exit(1);
            }
        }
    }

public:
    FileDB() {
        init_files();
    }

    ~FileDB() {
        if (index_map != MAP_FAILED) munmap(index_map, index_size);
        if (data_map != MAP_FAILED) munmap(data_map, data_size);
        if (index_fd >= 0) close(index_fd);
        if (data_fd >= 0) close(data_fd);
    }

    void insert(const std::string& index, int32_t value) {
        int32_t pos = hash_find(index);
        uint64_t new_node_id = next_node_id++;

        size_t node_offset = sizeof(uint64_t) + (new_node_id - 1) * sizeof(DataNode);
        ensure_data_size(node_offset + sizeof(DataNode));

        DataNode* new_node = get_data_node(new_node_id);
        new_node->value = value;
        new_node->next = 0;
        new_node->deleted = 0;
        save_next_node_id();

        if (pos == -1) {
            size_t entry_offset = sizeof(int32_t) + get_index_count() * sizeof(IndexEntry);
            ensure_index_size(entry_offset + sizeof(IndexEntry));

            IndexEntry* entry = get_index_entry(get_index_count());
            strncpy(entry->index, index.c_str(), MAX_INDEX_LEN - 1);
            entry->index[MAX_INDEX_LEN - 1] = '\0';
            entry->first_node = new_node_id;

            int32_t count = get_index_count();
            set_index_count(count + 1);
            hash_insert(index, count);
        } else {
            IndexEntry* entry = get_index_entry(pos);
            uint64_t prev = 0;
            uint64_t curr = entry->first_node;

            while (curr != 0) {
                DataNode* node = get_data_node(curr);
                if (node->deleted) {
                    prev = curr;
                    curr = node->next;
                    continue;
                }
                if (node->value == value) {
                    return;
                }
                if (node->value > value) {
                    break;
                }
                prev = curr;
                curr = node->next;
            }

            if (prev == 0) {
                new_node->next = entry->first_node;
                entry->first_node = new_node_id;
            } else {
                DataNode* prev_node = get_data_node(prev);
                new_node->next = prev_node->next;
                prev_node->next = new_node_id;
            }
        }
    }

    void del(const std::string& index, int32_t value) {
        int32_t pos = hash_find(index);
        if (pos == -1) return;

        IndexEntry* entry = get_index_entry(pos);
        uint64_t curr = entry->first_node;

        while (curr != 0) {
            DataNode* node = get_data_node(curr);
            if (!node->deleted && node->value == value) {
                node->deleted = 1;
                return;
            }
            curr = node->next;
        }
    }

    void find(const std::string& index) {
        int32_t pos = hash_find(index);
        if (pos == -1) {
            std::cout << "null" << std::endl;
            return;
        }

        IndexEntry* entry = get_index_entry(pos);
        uint64_t curr = entry->first_node;
        std::vector<int32_t> values;

        while (curr != 0) {
            DataNode* node = get_data_node(curr);
            if (!node->deleted) {
                values.push_back(node->value);
            }
            curr = node->next;
        }

        if (values.empty()) {
            std::cout << "null" << std::endl;
        } else {
            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) std::cout << " ";
                std::cout << values[i];
            }
            std::cout << std::endl;
        }
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    FileDB db;

    int n;
    std::cin >> n;

    for (int i = 0; i < n; i++) {
        std::string cmd;
        std::cin >> cmd;

        if (cmd == "insert") {
            std::string index;
            int32_t value;
            std::cin >> index >> value;
            db.insert(index, value);
        } else if (cmd == "delete") {
            std::string index;
            int32_t value;
            std::cin >> index >> value;
            db.del(index, value);
        } else if (cmd == "find") {
            std::string index;
            std::cin >> index;
            db.find(index);
        }
    }

    return 0;
}
