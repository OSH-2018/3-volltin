/*
 * source code of VTFS: volltin's filesystem.
 * Basic file and directory operations on dynamically allocated memory.
 */
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#include <fuse.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
using namespace std;

/*
 * Types for block id and node id.
 */
typedef long long BLKID_T;
typedef long long NODEID_T;

/*
 * PAGESIZE indicates the memeroy space that a page has.
 */
const size_t PAGESIZE = 4096;
/*
 * IDX_PER_PAGE indicates the number of block ids that can be store in a whole block.
 * SPC_PER_PAGE indicates the memroy space of block ids in a whole block can use. 
 */
const size_t IDX_PER_PAGE = PAGESIZE / sizeof(BLKID_T);
const size_t SPC_PER_PAGE = (IDX_PER_PAGE - 1) * (PAGESIZE);
/*
 * Everything is based on one (or more) block(s).
 * A block is a mmap-ed page. 
 */
const size_t MAX_BLK_ID = 1048576;

/*
 * Node includes: FILE, DIR, ContentNode
 */
const size_t MAX_NODE_ID = 1048576;

/*
 * Max length of filename is 255.
 */
const size_t FILENAME_LEN = 256;

/*
 * Every node has a type, file or dir. Type may decide the behaviour of node.
 */
enum NODETYPE_T {
    NODE_FILE, NODE_DIR
};

/*
 * Node is the most important object in the filesystem.
 * A node may be a file, dir, content.
 * attributes:
 *   node_type: see NODETYPE_T;
 *   node_id: a UNIQUE id of this node;
 *   blk_id: the block id of this node, see also MAX_BLK_ID;
 *   content: the block id of the node's content.
 *            For a file, the content store the block ids of its data.
 *            For a dir, the content store the block ids of its subnode.
 *            (It's not good to call this as `content`, `content_blk_id` is much more better :-(.
 *             But to refactor it, there's too many modifacitions that I can't make it before the ddl )
 *   st: stat (from `sys/stat.h`) of this node.
 *   name: the name of this node, the max length is FILENAME_LEN-1.
 */
struct Node
{
    NODETYPE_T node_type;
    NODEID_T node_id; // 0 for super node
    BLKID_T blk_id;   // blk id of this node
    BLKID_T content;  // first ContentNode id
    BLKID_T last_content; // last ContentNode id (not neccessary)
    struct stat st;
    char name[FILENAME_LEN];
    
    void set_node_type(NODETYPE_T _node_type) {
        node_type = _node_type;
    }
    
    void set_node_id(const NODEID_T _node_id) {
        node_id = _node_id;
    }

    void set_blk_id(BLKID_T _blk_id) {
        blk_id = _blk_id;
    }
    
    void set_content(BLKID_T _content) {
        content = _content;
    }
    
    void set_st(const struct stat& _st) {
        memcpy(&st, &_st, sizeof(struct stat));
    }
    
    void set_st(const struct stat* _st_ptr) {
        memcpy(&st, _st_ptr, sizeof(struct stat));
    }
    
    void set_name(const char* _name) {
        strncpy(name, _name, FILENAME_LEN);
    }
    
    void dumps()
    {
        printf("++ node: %lld ++\n", node_id);
        printf("++  - type: %s ++\n", node_type == NODE_FILE ? "FILE" : "DIR");
        printf("++  - name: %s ++\n", name);
        printf("++  - content: %lld ++\n", content);
    }
} NotExistsNode; // NotExistsNode indicats a failure attempt to get a node

/*
 * ContentNode is different from Node, there's no meta data and there's full of blk_ids.
 * For files:
 *  The fisrt IDX_PER_PAGE-1 ids are "pointer" to real data blk. the last id is the next ContentNode.
 * For dirs:
 *  The fisrt IDX_PER_PAGE-1 ids are "pointer" to blk id of subnodes. the last id is the next ContentNode.
 * See also Node::content
 */
struct ContentNode
{
    BLKID_T ids[IDX_PER_PAGE];
};


/* helper functions */

/*
 * get_default_stat
 * get a default `struct stat` for a file or dir.
 */
struct stat get_default_stat(bool dir = false) {
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    if (!dir) st.st_mode = S_IFREG | 0755;
    else st.st_mode = S_IFDIR | 0755;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    return st;
}

/* block functions */
void* blk_ids[MAX_BLK_ID];
BLKID_T get_new_blk_id() {
    for (BLKID_T i = 0; i < MAX_BLK_ID; i++)
        if (!blk_ids[i]) return i;
    return -1;
}

BLKID_T register_new_blk() {
    //printf("[*] Begin register_new_blk.\n");
    BLKID_T blk_id = get_new_blk_id();
    blk_ids[blk_id] = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(blk_ids[blk_id], 0, PAGESIZE);
    //printf("[*] ... registered %lld.\n", blk_id);
    return blk_id;
}

void free_blk_id(BLKID_T blk_id) {
    munmap(blk_ids[blk_id], PAGESIZE);
}

void write_to_blk(BLKID_T idx, const void* data, size_t size) {
    if (size == 0 or size > PAGESIZE) size = PAGESIZE;
    memcpy(blk_ids[idx], data, size);
}

void write_to_blk_offset(BLKID_T idx, const void* data, off_t offset, size_t size) {
    if (size == 0 or offset + size > PAGESIZE) size = PAGESIZE - offset;
    memcpy((char*)blk_ids[idx] + offset, data, size);
}

void read_from_blk(BLKID_T idx, void* data, size_t size) {
    if (size == 0 or size > PAGESIZE) size = PAGESIZE;
    memcpy(data, blk_ids[idx], size);
}

void read_from_blk_offset(BLKID_T idx, void* data, off_t offset, size_t size) {
    if (size == 0 or offset + size > PAGESIZE) size = PAGESIZE - offset;
    memcpy(data, (char*)blk_ids[idx] + offset, size);
}

/* node functions */
NODEID_T get_node_id()
{
    //("[*] Begin get_node_id.\n");
    static bool used[MAX_NODE_ID];
    for (NODEID_T i = 0; i < MAX_NODE_ID; i++)
    {
        if (!used[i])
        {
            used[i] = true;
            //printf("[*] ... get %lld.\n", i);
            return i;
        }
    }
    return -1;
}

void create_super_node()
{
    Node super_node;
    memset(&super_node, 0, sizeof(Node));
    super_node.set_node_id(get_node_id());
    super_node.set_blk_id(register_new_blk());
    super_node.set_node_type(NODE_DIR);
    super_node.set_name("/");
    super_node.set_content(register_new_blk());
    super_node.set_st(get_default_stat());
    write_to_blk(super_node.blk_id, &super_node, sizeof(Node));
}

Node get_node_by_blk_id(BLKID_T blk_id)
{
    Node t;
    read_from_blk(blk_id, &t, sizeof(Node));
    return t;
}

ContentNode get_content_node_by_blk_id(BLKID_T blk_id)
{
    ContentNode content_node;
    read_from_blk(blk_id, &content_node, sizeof(ContentNode));
    return content_node;
}

BLKID_T get_blk_id_of_node(NODEID_T tar_nid, BLKID_T cur_blkid = 0) {
    //printf("[+] get_blk_id_of_node tar_nid: %lld cur_blkid: %lld\n", tar_nid, cur_blkid);
    Node cur_node = get_node_by_blk_id(cur_blkid);

    if (cur_node.node_id == tar_nid) {
        return cur_node.blk_id;
    } else if (cur_node.node_type == NODE_DIR) { // search in dir
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

Node get_node_by_node_id(NODEID_T nid)
{
    if (nid == -1) {
        //printf("[E] get NotExistsNode!\n");
        return NotExistsNode;
    }
    return get_node_by_blk_id(get_blk_id_of_node(nid));
}

NODEID_T create_node(NODETYPE_T node_type, const char* name, NODEID_T parent_nid = 0, const struct stat* st = NULL)
{
    //printf("[*] Begin create node. (parent_nid = %lld)\n", parent_nid);
    NODEID_T nid = get_node_id();
    BLKID_T blk_id = register_new_blk();
    Node new_node;
    new_node.set_node_id(nid);
    new_node.set_blk_id(blk_id);
    new_node.set_node_type(node_type);
    new_node.set_name(name);
    new_node.set_content(register_new_blk());
    if (st) new_node.set_st(st);
    else new_node.set_st(get_default_stat(node_type == NODE_DIR));
    write_to_blk(blk_id, &new_node, sizeof(Node));
    Node parent_node = get_node_by_node_id(parent_nid);
    append_id_to_content_node(blk_id, parent_node.content);
    //printf("Create: %lld\n", nid);
    
    return nid;
}

/* api */
Node get_node_by_name_from_content(const char* target, ContentNode content) {
    //printf("[+] get_node_by_name_from_content(%s, content)\n", target);
    for (size_t i = 0; i < IDX_PER_PAGE - 1; i++) {
        if (content.ids[i] == 0) break;
        Node subnode = get_node_by_blk_id(content.ids[i]);
        if (strcmp(subnode.name, target) == 0)
            return subnode;
    }
    if (content.ids[IDX_PER_PAGE - 1]) {
        return get_node_by_name_from_content(target, get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]));
    }
    return NotExistsNode;
}

Node get_node_by_nid_from_content(NODEID_T target_nid, const ContentNode& content) {
    //printf("[+] get_node_by_nid_from_content(%lld, content)\n", target_nid);
    for (size_t i = 0; i < IDX_PER_PAGE - 1; i++) {
        if (content.ids[i] == 0) break;
        Node subnode = get_node_by_blk_id(content.ids[i]);
        if (subnode.node_id == target_nid)
            return subnode;
    }
    if (content.ids[IDX_PER_PAGE - 1]) {
        return get_node_by_nid_from_content(target_nid, get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]));
    }
    return NotExistsNode;
}

pair<size_t, BLKID_T> get_idx_by_blk_id_from_content(BLKID_T target_blk_id, BLKID_T content_blk) {
    ContentNode content = get_content_node_by_blk_id(content_blk);
    //printf("[+] get_idx_by_blk_id_from_content(%lld, content)\n", target_blk_id);
    for (size_t i = 0; i < IDX_PER_PAGE - 1; i++) {
        if (content.ids[i] == 0) break;
        if (content.ids[i] == target_blk_id)
            return pair<size_t, BLKID_T>(i, content_blk);
    }
    if (content.ids[IDX_PER_PAGE - 1]) {
        return get_idx_by_blk_id_from_content(target_blk_id, content.ids[IDX_PER_PAGE - 1]);
    }
    return pair<size_t, BLKID_T>(IDX_PER_PAGE, -1);
}

NODEID_T get_parent_nid(const Node& target_node, NODEID_T parent_nid = 0) {
    Node parent_node = get_node_by_node_id(parent_nid);
    if (parent_node.node_type != NODE_DIR) return -1;
    BLKID_T content_blk = parent_node.content;
    ContentNode content = get_content_node_by_blk_id(content_blk);
    Node subnode = get_node_by_nid_from_content(target_node.node_id, content);
    if (subnode.node_id != -1) {
        return parent_node.node_id;
    } else {
        while(content_blk) {
            ContentNode content = get_content_node_by_blk_id(content_blk);
            for (size_t i = 0; i < IDX_PER_PAGE - 1; i++) {
                BLKID_T node_blk = content.ids[i];
                if (node_blk == 0) break;

                Node node = get_node_by_blk_id(node_blk);
                NODEID_T ret = get_parent_nid(target_node, node.node_id);
                if (ret != -1) return ret;
            }
            content_blk = content.ids[IDX_PER_PAGE - 1];
        }
    }
    return -1;
}

Node get_node_by_path(const char* path, NODEID_T parent_nid = 0) {
    //printf("[+] get_node_by_path(\"%s\", %lld)\n", path, parent_nid);
    if (strlen(path) == 0) return get_node_by_node_id(parent_nid);
    char target[FILENAME_LEN];
    memset(target, 0, sizeof(target));
    const char* pos = strchr(path, '/');
    if (pos == NULL) strcpy(target, path);
    else memcpy(target, path, (pos - path) * sizeof(char));
    Node subnode = get_node_by_name_from_content(target, get_content_node_by_blk_id(get_node_by_node_id(parent_nid).content));
    // if subnode == NotEN
    if (subnode.node_id == -1) {
        return subnode;
    }
    if (pos == NULL)
        return subnode;
    else
        return get_node_by_path(pos + 1, subnode.node_id);
}

void create_node_by_path(const char* path, const struct stat* st, NODEID_T parent_nid = 0, NODETYPE_T node_type = NODE_FILE) {
    //printf("[+] create_node_by_path path=%s parent_node=%lld\n", path, parent_nid);
    char target[FILENAME_LEN];
    const char* pos = strchr(path, '/');
    if (pos == NULL) strcpy(target, path);
    else{
        memcpy(target, path, (pos - path));
        target[pos - path] = '\0';
    }
    Node parent_node = get_node_by_node_id(parent_nid);
    if (pos == NULL)
        create_node(node_type, target, parent_node.node_id, st);
    else {
        Node curnode = get_node_by_name_from_content(target, get_content_node_by_blk_id(parent_node.content));
        create_node_by_path(pos + 1, st, curnode.node_id, node_type);
    }
}

void read_from_node(const Node& node, char* buf, off_t offset, off_t size) {
    BLKID_T content_blk = node.content;
    
    // calculate start point
    off_t ctn_offset = offset / SPC_PER_PAGE;
    off_t idx_offset = (offset - ctn_offset * SPC_PER_PAGE ) / PAGESIZE;
    off_t blk_offset = (offset - ctn_offset * SPC_PER_PAGE - idx_offset * PAGESIZE);
    while(ctn_offset--) content_blk = get_content_node_by_blk_id(content_blk).ids[IDX_PER_PAGE - 1];
    
    ContentNode content = get_content_node_by_blk_id(content_blk);
    
    // read first block
    off_t read_size = PAGESIZE - blk_offset;
    if (size < read_size) read_size = size;
    read_from_blk_offset(content.ids[idx_offset], buf, blk_offset, read_size);
    buf += read_size;
    size -= read_size;
    idx_offset += 1;
    if (idx_offset == IDX_PER_PAGE - 1) {
        content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
        idx_offset = 0;   
    }
    
    while(size > 0) {
        if (size > PAGESIZE) {    
            read_from_blk_offset(content.ids[idx_offset], buf, 0, PAGESIZE);
            buf += PAGESIZE;
            size -= PAGESIZE;
            idx_offset += 1;
            if (idx_offset == IDX_PER_PAGE - 1) {
                content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
                idx_offset = 0;
            }
        } else {    
            read_from_blk_offset(content.ids[idx_offset], buf, 0, size);
            buf += size;
            size -= size;
            idx_offset += 1;
            if (idx_offset == IDX_PER_PAGE - 1) {
                content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
                idx_offset = 0;           
            }
        }
    }    
}


void write_to_node(const Node& node, const char* buf, off_t offset, off_t size) {
    BLKID_T content_blk = node.content;
    
    // calculate start point
    off_t ctn_offset = offset / SPC_PER_PAGE;
    off_t idx_offset = (offset - ctn_offset * SPC_PER_PAGE ) / PAGESIZE;
    off_t blk_offset = (offset - ctn_offset * SPC_PER_PAGE - idx_offset * PAGESIZE);
    while(ctn_offset--) content_blk = get_content_node_by_blk_id(content_blk).ids[IDX_PER_PAGE - 1];

    ContentNode content = get_content_node_by_blk_id(content_blk);
    
    // read first block
    off_t read_size = PAGESIZE - blk_offset;
    if (size < read_size) read_size = size;

    if (content.ids[idx_offset] == 0) content.ids[idx_offset] = register_new_blk();
    write_to_blk(content_blk, &content, sizeof(ContentNode));


    write_to_blk_offset(content.ids[idx_offset], buf, blk_offset, read_size);
    buf += read_size;
    size -= read_size;
    idx_offset += 1;
    if (idx_offset == IDX_PER_PAGE - 1) {
        content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
        idx_offset = 0;
    }

    while(size > 0) {
        if (size > PAGESIZE) {
    
            if (content.ids[idx_offset] == 0) content.ids[idx_offset] = register_new_blk();
            write_to_blk(content_blk, &content, sizeof(ContentNode));
            write_to_blk_offset(content.ids[idx_offset], buf, 0, PAGESIZE);
            buf += PAGESIZE;
            size -= PAGESIZE;
            idx_offset += 1;
            if (idx_offset == IDX_PER_PAGE - 1) {
                content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
                idx_offset = 0;
            }
        } else {
            if (content.ids[idx_offset] == 0) content.ids[idx_offset] = register_new_blk();
            write_to_blk(content_blk, &content, sizeof(ContentNode));
            write_to_blk_offset(content.ids[idx_offset], buf, 0, size);
            buf += size;
            size -= size;
            idx_offset += 1;
            if (idx_offset == IDX_PER_PAGE - 1) {
                content = get_content_node_by_blk_id(content.ids[IDX_PER_PAGE - 1]);
                idx_offset = 0;                
            }
        }
    }
}

void free_content_blk(BLKID_T content_blk) {
    while(content_blk) {
        ContentNode content = get_content_node_by_blk_id(content_blk);
        for (off_t i = 0; i < IDX_PER_PAGE - 1; i++) {
            BLKID_T blk_id = content.ids[i];
            if (blk_id == 0) break;
            else free_blk_id(blk_id);
        }
        free_blk_id(content_blk);
        content_blk = content.ids[IDX_PER_PAGE - 1];
    }
}

void realloc_node_size(Node node, size_t size) {
    //printf("[+] realloc_node_size node_id=%lld, size=%lu\n", node.node_id, size);
    off_t old_size = node.st.st_size;
    off_t new_size = size;
    
    off_t old_content_blk_num = old_size / SPC_PER_PAGE + 1;
    off_t new_content_blk_num = new_size / SPC_PER_PAGE + 1;
    if (new_size >= old_size) {
        // find last page
        // BLKID_T content_blk = node.content;
        // for (off_t _ = 0; _ + 1 < old_content_blk_num; _++) {
        //     ContentNode content = get_content_node_by_blk_id(content_blk);
        //     content_blk = content.ids[IDX_PER_PAGE - 1];
        // }
        BLKID_T content_blk = node.last_content;
        off_t new_page_num = new_content_blk_num - old_content_blk_num;
        
        ContentNode content = get_content_node_by_blk_id(content_blk);
        for (off_t _ = 0; _ < new_page_num; _++) {
            content = get_content_node_by_blk_id(content_blk);
            content.ids[IDX_PER_PAGE-1] = register_new_blk();
            write_to_blk(content_blk, &content, sizeof(ContentNode));
            content_blk = content.ids[IDX_PER_PAGE-1];
        }
        node.last_content = content.ids[IDX_PER_PAGE-1];
    } else {
        
        BLKID_T  cur_content_blk = node.content;
        BLKID_T next_content_blk = node.content;
        for (size_t _ = 0; _ < new_content_blk_num; _++) {
            cur_content_blk = next_content_blk;
            ContentNode content = get_content_node_by_blk_id(cur_content_blk);
            next_content_blk = content.ids[IDX_PER_PAGE - 1];
        }
        if (next_content_blk) {
            ContentNode content = get_content_node_by_blk_id(cur_content_blk);
            content.ids[IDX_PER_PAGE-1] = 0;
            write_to_blk(cur_content_blk, &content, sizeof(ContentNode));
            free_content_blk(next_content_blk);
        }
        node.last_content = cur_content_blk;
    }
    node.st.st_size = size;
    write_to_blk(node.blk_id, &node, sizeof(Node));
}

void shift_left_content(off_t idx, BLKID_T content_blk) {
    ContentNode content = get_content_node_by_blk_id(content_blk);
    for (off_t i = idx + 1; i < IDX_PER_PAGE - 1; i++) {
        content.ids[i-1] = content.ids[i];
    }
    BLKID_T next_content_blk = content.ids[IDX_PER_PAGE - 1];
    if (next_content_blk) {
        ContentNode next_content = get_content_node_by_blk_id(next_content_blk);
        content.ids[IDX_PER_PAGE - 2] = next_content.ids[0];
        shift_left_content(0, next_content_blk);
    }
    write_to_blk(content_blk, &content, sizeof(ContentNode));
}

void remove_record_from_content(BLKID_T to_remove, BLKID_T content_blk) {
    pair<off_t, BLKID_T> ret = get_idx_by_blk_id_from_content(to_remove, content_blk);
    shift_left_content(ret.first, ret.second);
}

void remove_node(const Node& node) {
    // release all space of the node
    realloc_node_size(node, 0);
    // delete record from parent
    Node parent_node = get_node_by_node_id(get_parent_nid(node));
    remove_record_from_content(node.blk_id, parent_node.content);
    // free all the space
    free_blk_id(node.content);
    free_blk_id(node.blk_id);
}

/* fuse functions */

static void *vtfs_init(struct fuse_conn_info *conn) {
    printf("[.] vtfs_init\n");
    NotExistsNode.node_id = -1;
    create_super_node();
    return NULL;
}

static int vtfs_getattr(const char *path, struct stat *stbuf)
{
    printf("[.] vtfs_getattr path=%s\n", path);
    int ret = 0;
    
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_uid = fuse_get_context()->uid;
        stbuf->st_gid = fuse_get_context()->gid;
    } else {
        Node node = get_node_by_path(path + 1);
        if (node.node_id == -1) {
            ret = -ENOENT;
        } else {
            memcpy(stbuf, &node.st, sizeof(struct stat));
        }
    }
    return ret;
}

static int vtfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    printf("[.] vtfs_readdir path=%s\n", path);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    Node node = get_node_by_path(path + 1);
    if (node.node_type == NODE_DIR) {
        BLKID_T content_blk = node.content;
        while(content_blk) {
            ContentNode content = get_content_node_by_blk_id(content_blk);
            for (off_t i = 0; i < IDX_PER_PAGE - 1; i++) {
                if (content.ids[i] == 0) break;
                Node subnode = get_node_by_blk_id(content.ids[i]);
                filler(buf, subnode.name, &subnode.st, 0);
            }
            content_blk = content.ids[IDX_PER_PAGE - 1];
        }
    }
    return 0;
}

static int vtfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    printf("[.] vtfs_mknod\n");
    struct stat st  = get_default_stat();
    create_node_by_path(path + 1, &st);
    return 0;
}

static int vtfs_mkdir(const char *path, mode_t mode)
{
    printf("[.] vtfs_mkdir\n");
    struct stat st = get_default_stat(true);
    create_node_by_path(path + 1, &st, 0, NODE_DIR);
    return 0;
}

static int vtfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("[.] vtfs_read\n");
    Node node = get_node_by_path(path + 1);
    if (node.node_id == -1)
        return -ENOENT;
    off_t ret = size;
    if(offset + size > node.st.st_size)
        ret = node.st.st_size - offset;
    read_from_node(node, buf, offset, ret);
    return ret;
}

static int vtfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("[.] vtfs_write\n");
    Node node = get_node_by_path(path + 1);
    if (node.node_id == -1)
        return -ENOENT;
    off_t new_size = node.st.st_size;
    if (offset + size > new_size) {
        new_size = offset + size;
    }
    realloc_node_size(node, new_size);
    write_to_node(node, buf, offset, size);
    return size;
}

static int vtfs_truncate(const char *path, off_t size)
{
    printf("[.] vtfs_truncate\n");
    Node node = get_node_by_path(path + 1);
    if (node.node_id == -1)
        return -ENOENT;
    realloc_node_size(node, size);
    return 0;
}

static int vtfs_unlink(const char *path)
{
    printf("[.] vtfs_unlink\n");
    Node node = get_node_by_path(path + 1);
    if (node.node_id == -1)
        return -ENOENT;
    remove_node(node);
    return 0;
}

static int vtfs_rmdir(const char *path)
{
    printf("[.] vtfs_rmdir\n");
    Node node = get_node_by_path(path + 1);
    if (node.node_id == -1)
        return -ENOENT;
    remove_node(node);
    return 0;
}

static int vtfs_open(const char *path, struct fuse_file_info *fi)
{
    printf("[.] vtfs_open\n");
    return 0;
}

/* main */

int main(int argc, char *argv[])
{
    struct fuse_operations op;
    memset(&op, 0, sizeof(op));
    op.init = vtfs_init;
    op.getattr = vtfs_getattr;
    op.readdir = vtfs_readdir;
    op.mknod = vtfs_mknod;
    op.open = vtfs_open;
    op.write = vtfs_write;
    op.truncate = vtfs_truncate;
    op.read = vtfs_read;
    op.unlink = vtfs_unlink;
    op.rmdir = vtfs_rmdir;
    op.mkdir = vtfs_mkdir;
    return fuse_main(argc, argv, &op, NULL);
}
