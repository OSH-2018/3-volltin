#include <cstdio>
#include <sys/stat.h>
#include <sys/mman.h>
#include <vector>
#include <cstring>
#include <string>
#include <algorithm>
using namespace std;

typedef long long BLKID_T;
typedef int NODEID_T;

const size_t PAGE_SIZE = 4096;
const size_t IDX_PER_PAGE = PAGE_SIZE / sizeof(BLKID_T);
const size_t MAX_BLK_ID = 1048576;

const size_t FILENAME_LEN = 256;

enum NODETYPE_T {
    NODE_FILE, NODE_DIR
};

struct Node
{
    NODETYPE_T node_type;
    NODEID_T node_id; // 0 for super node
    NODEID_T next_id; // 0 for no next one
    BLKID_T content;  // first ContentNode id
    //struct stat state;
    char name[FILENAME_LEN];

    void set_name(const char* _name) {
        strncpy(name, _name, FILENAME_LEN);
    }

    void set_node_type(NODETYPE_T _node_type) {
        node_type = _node_type;
    }

    void set_content(BLKID_T _content) {
        content = _content;
    }
};

struct ContentNode
{
    // last idx store the next ContentNode, 0 for no next one.
    BLKID_T ids[IDX_PER_PAGE];
};

/* blk */
void* blk_ids[MAX_BLK_ID];

BLKID_T get_new_blk_id()
{
    for (BLKID_T i = 0; i < MAX_BLK_ID; i++)
        if (!blk_ids[i]) return i;
}

BLKID_T register_new_blk()
{
    BLKID_T idx = get_new_blk_id(); 
    blk_ids[idx] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(blk_ids[idx], 0, PAGE_SIZE);
    return idx;
}

void write_to_blk(BLKID_T idx, const void* data, size_t size)
{
    if (size == 0 or size > PAGE_SIZE) size = PAGE_SIZE;
    memcpy(blk_ids[idx], data, size);
}

void read_from_blk(BLKID_T idx, void* data, size_t size)
{
    if (size == 0 or size > PAGE_SIZE) size = PAGE_SIZE;
    memcpy(data, blk_ids[idx], size);
}

void create_super_node()
{
    BLKID_T idx = register_new_blk();
    Node super_node;
    memset(&super_node, 0, sizeof(Node));
    super_node.node_id = 0;
    super_node.set_node_type(NODE_DIR);
    super_node.set_name("/");
    super_node.set_content(register_new_blk());
}

Node get_node_by_blk_id(BLKID_T blk_id)
{
    Node t;
    read_from_blk(blk_id, &t, sizeof(Node));
    return t;
}

ContentNode get_content_node_by_blk_id(BLKID_T blk_id)
{
    ContentNode t;
    read_from_blk(blk_id, &t, sizeof(ContentNode));
    return t;
}

BLKID_T get_blk_id_of_node(NODEID_T tar_nid, BLKID_T cur_blkid = 0) {
    Node cur_node = get_node_by_blk_id(cur_blkid);
    if (cur_node.node_id == tar_nid) {
        return cur_blkid;
    } else {
        ContentNode content = get_content_node_by_blk_id(cur_node.content);
        for (size_t i = 0; i < IDX_PER_PAGE - 1; i++)
        {
            if (content.ids[i] == 0) {
                return -1;
            }
            else {
                BLKID_T id = get_blk_id_of_node(tar_nid, content.ids[i]);
                if (id != -1) return id;
            }
        }
        if (content.ids[IDX_PER_PAGE - 1])
        {
            return get_blk_id_of_node(tar_nid, content.ids[IDX_PER_PAGE - 1]);
        }
    }
    return -1;
}

NODETYPE_T get_node_type(NODEID_T nid)
{
    assert(nid >= 0);
    BLKID_T blk_id = get_blk_id_of_node(nid);
    Node node = get_node_by_blk_id(blk_id);
    return node.node_type;
}

void append_id_to_content_node(BLKID_T to_append, BLKID_T blk_id)
{
    ContentNode content = get_content_node_by_blk_id(blk_id);
    if (content.ids[IDX_PER_PAGE - 1]) {
        append_id_to_content_node(to_append, content.ids[IDX_PER_PAGE - 1]);
    } else {
        size_t target = IDX_PER_PAGE;
        for (size_t i = 0; i < IDX_PER_PAGE - 1; i++) {
            if (content.ids[i] == 0) {
                target = i;
                break;
            }
        }
        if (target == IDX_PER_PAGE) { // not fount space
            content.ids[IDX_PER_PAGE - 1] = register_new_blk();
            append_id_to_content_node(to_append, content.ids[IDX_PER_PAGE - 1]);
        } else {
            content.ids[target] = to_append;
            write_to_blk(blk_id, &content, sizeof(ContentNode));
        }
    }
}

NODEID_T register_node(NODEID_T parrent = 0)
{
    
}

void create_file(const char* name, const char* data)
{
    NODEID_T nid = register_node();
}

int main()
{
    printf("%u", sizeof(ContentNode));
    return 0;
}