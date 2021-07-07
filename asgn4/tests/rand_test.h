// Header file for rand_test.c

#define MAX_NAME_LEN 255       // max generated filename length
#define MAX_NUM_CHAR 1024      // maximum number of chars to write

// character array for generating random filename
static char abc[26] = "abcdefghijklmnopqrstuvwxyz";

// Function declarations
int test_random(void);
