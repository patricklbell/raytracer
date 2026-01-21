static void lvbh_dump_node(FILE* f, const LBVH_Node* node, int* id_counter) {
    if (!node) {
        fprintf(f, "NULL\n");
        return;
    }

    int id = (*id_counter)++;

    fprintf(f,
        "NODE %d %f %f %f %f %f %f\n",
        id,
        node->aabb.min.x,
        node->aabb.min.y,
        node->aabb.min.z,
        node->aabb.max.x,
        node->aabb.max.y,
        node->aabb.max.z
    );

    lvbh_dump_node(f, node->left, id_counter);
    lvbh_dump_node(f, node->right, id_counter);
}

internal void lbvh_dump_tree(const LBVH_Tree* tree, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "LBVH 1\n");

    int id_counter = 0;
    lvbh_dump_node(f, tree->root, &id_counter);

    fclose(f);
}
