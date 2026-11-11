#include "applications.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Expected a desktop-entry fixture path.\n");
        return 2;
    }

    Application application;
    if (!application_load_desktop_file(argv[1], &application, 0)) {
        fprintf(stderr, "Could not load terminal application fixture.\n");
        return 1;
    }
    if (!application.terminal ||
        strcmp(application.name, "Terminal test fixture") != 0) {
        fprintf(stderr, "Terminal application metadata was not preserved.\n");
        return 1;
    }
    return 0;
}
