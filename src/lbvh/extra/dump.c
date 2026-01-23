static void lvbh_dump_node(FILE* f, const LBVH_Node* node) {
    u8 sentinel = (node == NULL) ? 0 : 1;
    fwrite(&sentinel, sizeof(sentinel), 1, f);
    if (node == NULL)
        return;

    fwrite(&node->id, sizeof(node->id), 1, f);
    fwrite(&node->aabb.min, sizeof(node->aabb.min), 1, f);
    fwrite(&node->aabb.max, sizeof(node->aabb.max), 1, f);

    lvbh_dump_node(f, node->left);
    lvbh_dump_node(f, node->right);
}

internal void lbvh_dump_tree(const LBVH_Tree* tree, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;

    lvbh_dump_node(f, tree->root);

    fclose(f);
}
