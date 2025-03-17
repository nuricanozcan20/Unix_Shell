#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 1024  // max nodes in the tree
#define MAX_DEPTH 128   // max depth of the tree

//we firstly create the dot file to create the graph. Then we will use this dot file to create the png 
//with graphviz
int generate_dot_file(const char *input_file, const char *dot_file) {
    FILE *input = fopen(input_file, "r");
    if (!input) {
        perror("ERROR while opening input file");
        return -1;
    }

    FILE *graph_file = fopen(dot_file, "w");
    if (!graph_file) {
        perror("ERROR whilecreating Graphviz file");
        fclose(input);
        return -1;
    }

    // this is the title
    fprintf(graph_file, "digraph ProcessTree {\n");
    fprintf(graph_file, "    node [shape=box];\n");

    // To track the parent-child relationship
    char parent_stack[MAX_DEPTH][256];
    int current_depth = 0;

    char line[256];
    while (fgets(line, sizeof(line), input)) {
        char *pid_str = strstr(line, "PID: ");
        char *cmd_str = strstr(line, "Command: ");

        if (pid_str && cmd_str) {
            int pid;
            char command[256];
            sscanf(pid_str, "PID: %d,", &pid);
            sscanf(cmd_str, "Command: %s", command);

            // we firstly create the current node
            char current_node[256];
            snprintf(current_node, sizeof(current_node), "node%d", pid);
            fprintf(graph_file, "    %s [label=\"%s\\nPID: %d\"];\n", current_node, command, pid);

            // Determine the depth based on indentation
            int depth = 0;
            while (line[depth] == ' ' || line[depth] == '-') {
                depth++;
            }
            depth /= 2;  // Each indentation level adds 2 spaces

            if (depth > current_depth) {
                // moving deeper into the tree
                current_depth = depth;
            } else if (depth < current_depth) {
        	//moving up
                current_depth = depth;
            }

            // Record the parent-child relationship
            if (current_depth > 0) {
                fprintf(graph_file, "    %s -> %s;\n", parent_stack[current_depth - 1], current_node);
            }

            //we update the parent stack
            strcpy(parent_stack[current_depth], current_node);
        }
    }

    fprintf(graph_file, "}\n");
    fclose(input);
    fclose(graph_file);
    return 0;
}

//this function generates the png file according to the created dot file
int generate_image_file(const char *dot_file, const char *output_file) {
	const char *ext = strrchr(output_file,'.');
	if(!ext){
		fprintf(stderr,"Error: supported extensions : .png, .jpeg, .jpg\n");
	}
	ext++;
    char command[512];
    snprintf(command, sizeof(command), "dot -T%s %s -o %s",ext, dot_file, output_file);
    if (system(command) != 0) {
        fprintf(stderr, "Error generating graph image with Graphviz.\n");
        return -1;
    }
    return 0;
}
