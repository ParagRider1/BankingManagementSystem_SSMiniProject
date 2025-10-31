#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define DB_FILE "data/accounts.dat"
#define MAX_NAME 64
#define MAX_PASS 32
#define MAX_CLIENT_MSG 512
#define ADMIN_USER "admin"
#define ADMIN_PASS "admin123"

// account types
#define ACC_NORMAL 1
#define ACC_JOINT 2
#define ACC_ADMIN 9

typedef struct Account {
    int acc_no;                 // unique account number (>=1)
    int acc_type;               // ACC_NORMAL or ACC_JOINT
    char name1[MAX_NAME];
    char name2[MAX_NAME];       // for joint accounts; empty otherwise
    char password[MAX_PASS];
    double balance;
    int active;                 // 1 active, 0 deleted
    char padding[32];           // pad to make record size stable
} Account;

#endif
