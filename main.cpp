#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>

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
    std::fstream index_file;
    std::fstream data_file;
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
                IndexEntry entry = read_index_entry(hash_table[idx].pos);
                if (strcmp(entry.index, index.c_str()) == 0) {
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
                IndexEntry entry = read_index_entry(hash_table[idx].pos);
                if (strcmp(entry.index, index.c_str()) == 0) {
                    hash_table[idx].hash = EMPTY_HASH;
                    return;
                }
            }
        }
    }

    void init_files() {
        index_file.open(INDEX_FILE, std::ios::in | std::ios::out | std::ios::binary);
        if (!index_file) {
            index_file.open(INDEX_FILE, std::ios::out | std::ios::binary);
            int32_t count = 0;
            index_file.write(reinterpret_cast<char*>(&count), sizeof(count));
            index_file.close();
            index_file.open(INDEX_FILE, std::ios::in | std::ios::out | std::ios::binary);
        }

        data_file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);
        if (!data_file) {
            data_file.open(DATA_FILE, std::ios::out | std::ios::binary);
            next_node_id = 1;
            data_file.write(reinterpret_cast<char*>(&next_node_id), sizeof(next_node_id));
            data_file.close();
            data_file.open(DATA_FILE, std::ios::in | std::ios::out | std::ios::binary);
        } else {
            data_file.read(reinterpret_cast<char*>(&next_node_id), sizeof(next_node_id));
        }

        hash_table.resize(HASH_SIZE);
        for (auto& entry : hash_table) {
            entry.hash = EMPTY_HASH;
        }

        rebuild_hash_table();
    }

    void rebuild_hash_table() {
        int32_t count = get_index_count();
        for (int32_t i = 0; i < count; i++) {
            IndexEntry entry = read_index_entry(i);
            hash_insert(entry.index, i);
        }
    }

    int32_t get_index_count() {
        int32_t count;
        index_file.seekg(0, std::ios::beg);
        index_file.read(reinterpret_cast<char*>(&count), sizeof(count));
        return count;
    }

    void set_index_count(int32_t count) {
        index_file.seekp(0, std::ios::beg);
        index_file.write(reinterpret_cast<char*>(&count), sizeof(count));
    }

    IndexEntry read_index_entry(int32_t pos) {
        IndexEntry entry;
        index_file.seekg(sizeof(int32_t) + pos * sizeof(IndexEntry), std::ios::beg);
        index_file.read(reinterpret_cast<char*>(&entry), sizeof(IndexEntry));
        return entry;
    }

    void write_index_entry(int32_t pos, const IndexEntry& entry) {
        index_file.seekp(sizeof(int32_t) + pos * sizeof(IndexEntry), std::ios::beg);
        index_file.write(reinterpret_cast<const char*>(&entry), sizeof(IndexEntry));
    }

    DataNode read_data_node(uint64_t node_id) {
        DataNode node;
        data_file.seekg(sizeof(uint64_t) + (node_id - 1) * sizeof(DataNode), std::ios::beg);
        data_file.read(reinterpret_cast<char*>(&node), sizeof(DataNode));
        return node;
    }

    void write_data_node(uint64_t node_id, const DataNode& node) {
        data_file.seekp(sizeof(uint64_t) + (node_id - 1) * sizeof(DataNode), std::ios::beg);
        data_file.write(reinterpret_cast<const char*>(&node), sizeof(DataNode));
    }

    void save_next_node_id() {
        data_file.seekp(0, std::ios::beg);
        data_file.write(reinterpret_cast<char*>(&next_node_id), sizeof(next_node_id));
    }

public:
    FileDB() {
        init_files();
    }

    ~FileDB() {
        if (index_file.is_open()) index_file.close();
        if (data_file.is_open()) data_file.close();
    }

    void insert(const std::string& index, int32_t value) {
        int32_t pos = hash_find(index);
        uint64_t new_node_id = next_node_id++;

        DataNode new_node;
        new_node.value = value;
        new_node.next = 0;
        new_node.deleted = 0;
        write_data_node(new_node_id, new_node);
        save_next_node_id();

        if (pos == -1) {
            IndexEntry entry;
            strncpy(entry.index, index.c_str(), MAX_INDEX_LEN - 1);
            entry.index[MAX_INDEX_LEN - 1] = '\0';
            entry.first_node = new_node_id;

            int32_t count = get_index_count();
            set_index_count(count + 1);
            write_index_entry(count, entry);
            hash_insert(index, count);
        } else {
            IndexEntry entry = read_index_entry(pos);
            uint64_t prev = 0;
            uint64_t curr = entry.first_node;

            while (curr != 0) {
                DataNode node = read_data_node(curr);
                if (node.deleted) {
                    prev = curr;
                    curr = node.next;
                    continue;
                }
                if (node.value == value) {
                    return;
                }
                if (node.value > value) {
                    break;
                }
                prev = curr;
                curr = node.next;
            }

            if (prev == 0) {
                new_node.next = entry.first_node;
                write_data_node(new_node_id, new_node);
                entry.first_node = new_node_id;
                write_index_entry(pos, entry);
            } else {
                DataNode prev_node = read_data_node(prev);
                new_node.next = prev_node.next;
                write_data_node(new_node_id, new_node);
                prev_node.next = new_node_id;
                write_data_node(prev, prev_node);
            }
        }
    }

    void del(const std::string& index, int32_t value) {
        int32_t pos = hash_find(index);
        if (pos == -1) return;

        IndexEntry entry = read_index_entry(pos);
        uint64_t curr = entry.first_node;

        while (curr != 0) {
            DataNode node = read_data_node(curr);
            if (!node.deleted && node.value == value) {
                node.deleted = 1;
                write_data_node(curr, node);
                return;
            }
            curr = node.next;
        }
    }

    void find(const std::string& index) {
        int32_t pos = hash_find(index);
        if (pos == -1) {
            std::cout << "null" << std::endl;
            return;
        }

        IndexEntry entry = read_index_entry(pos);
        uint64_t curr = entry.first_node;
        std::vector<int32_t> values;

        while (curr != 0) {
            DataNode node = read_data_node(curr);
            if (!node.deleted) {
                values.push_back(node.value);
            }
            curr = node.next;
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
