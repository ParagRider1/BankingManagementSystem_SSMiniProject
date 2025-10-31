#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define DB_ACC_FILE "data/accounts.dat"
#define DB_LOAN_FILE "data/loans.dat"
#define WAL_FILE "data/wal.log"

#define MAX_NAME 64
#define MAX_PASS 32
#define MAX_MSG 512

/* Account types */
#define ROLE_CUSTOMER 1
#define ROLE_EMPLOYEE 2
#define ROLE_MANAGER  3
#define ROLE_ADMIN    9

/* Loan states */
#define LOAN_PENDING 0
#define LOAN_REVIEWED 1
#define LOAN_APPROVED 2
#define LOAN_REJECTED 3

/* Simple admin credentials (for demo) */
#define ADMIN_PASS "admin123"

/* Account record */
typedef struct {
    int acc_no;                 // 1-based
    int role;                   // ROLE_CUSTOMER / other roles for special accounts
    char name[MAX_NAME];
    char password[MAX_PASS];
    double balance;
    int active;                 // 1 active, 0 deleted
    char padding[32];
} Account;

/* Loan record */
typedef struct {
    int loan_id;                // 1-based
    int acc_no;                 // applicant
    double amount;
    int status;                 // LOAN_PENDING, REVIEWED, APPROVED, REJECTED
    char purpose[128];
    char padding[32];
} Loan;

/* WAL entry types: we'll store simple text entries:
   BEGIN TXID
   ACTION ... (e.g., DEBIT acc_no amount)
   ACTION CREDIT ...
   COMMIT TXID
*/
#define TX_BUF 1024

#endif
