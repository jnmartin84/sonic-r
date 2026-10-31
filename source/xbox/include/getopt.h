/*
 * getopt.h - stub for nxdk. The Xbox has no command line, so getopt_long()
 * always reports "no more options" and main() keeps its defaults.
 */
#ifndef XBOX_GETOPT_H
#define XBOX_GETOPT_H

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

#define no_argument        0
#define required_argument  1
#define optional_argument  2

static char *optarg = 0;
static int   optind = 1;

static inline int getopt_long(int argc, char *const argv[], const char *shortopts,
                              const struct option *longopts, int *longindex)
{
    (void)argc; (void)argv; (void)shortopts; (void)longopts; (void)longindex;
    return -1;
}

#endif /* XBOX_GETOPT_H */
