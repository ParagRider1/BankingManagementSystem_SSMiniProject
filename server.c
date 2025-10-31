// server.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <time.h>

#include "common.h"

#define PORT 8080
#define BACKLOG 10

/* Global DB file descriptors */
int fd_acc = -1;
int fd_loan = -1;
int fd_wal = -1;

/* Utility: ensure data dir and files exist */
void ensure_files() {
    system("mkdir -p data");
    fd_acc = open(DB_ACC_FILE, O_RDWR | O_CREAT, 0666);
    if (fd_acc < 0) { perror("open accounts"); exit(1); }
    fd_loan = open(DB_LOAN_FILE, O_RDWR | O_CREAT, 0666);
    if (fd_loan < 0) { perror("open loans"); exit(1); }
    fd_wal = open(WAL_FILE, O_RDWR | O_CREAT | O_APPEND, 0666);
    if (fd_wal < 0) { perror("open wal"); exit(1); }
}

/* Low-level record locking by record index and length (advisory) */
int lock_by_offset(int fd, off_t start, off_t len, short lock_type) {
    struct flock fl;
    fl.l_type = lock_type; // F_RDLCK / F_WRLCK / F_UNLCK
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    fl.l_pid = 0;
    return fcntl(fd, F_SETLKW, &fl); // blocking
}

/* Helpers for account file (1-based indexing of records) */
ssize_t read_account_by_no(int acc_no, Account *acc_out) {
    off_t off = (off_t)(acc_no - 1) * sizeof(Account);
    if (lseek(fd_acc, off, SEEK_SET) < 0) return -1;
    return read(fd_acc, acc_out, sizeof(Account));
}
ssize_t write_account_by_no(int acc_no, Account *acc) {
    off_t off = (off_t)(acc_no - 1) * sizeof(Account);
    if (lseek(fd_acc, off, SEEK_SET) < 0) return -1;
    ssize_t w = write(fd_acc, acc, sizeof(Account));
    fsync(fd_acc);
    return w;
}

/* Helpers for loan file */
ssize_t read_loan_by_id(int loan_id, Loan *loan_out) {
    off_t off = (off_t)(loan_id - 1) * sizeof(Loan);
    if (lseek(fd_loan, off, SEEK_SET) < 0) return -1;
    return read(fd_loan, loan_out, sizeof(Loan));
}
ssize_t write_loan_by_id(int loan_id, Loan *loan) {
    off_t off = (off_t)(loan_id - 1) * sizeof(Loan);
    if (lseek(fd_loan, off, SEEK_SET) < 0) return -1;
    ssize_t w = write(fd_loan, loan, sizeof(Loan));
    fsync(fd_loan);
    return w;
}

/* Allocate next account number (append) */
int next_account_no() {
    struct stat st;
    fstat(fd_acc, &st);
    return (int)(st.st_size / sizeof(Account)) + 1;
}
int next_loan_id() {
    struct stat st;
    fstat(fd_loan, &st);
    return (int)(st.st_size / sizeof(Loan)) + 1;
}

/* WAL helpers: append line to WAL (with simple locking) */
int wal_append_line(const char *line) {
    // Acquire exclusive lock for WAL file (threads/processes) using flock
    if (flock(fd_wal, LOCK_EX) < 0) return -1;
    ssize_t n = write(fd_wal, line, strlen(line));
    write(fd_wal, "\n", 1);
    fsync(fd_wal);   // ensure durability of WAL
    flock(fd_wal, LOCK_UN);
    return (n >= 0) ? 0 : -1;
}

/* WAL transaction: append BEGIN, actions..., COMMIT.
   For simplicity each transaction will be a sequence of action lines; actions are applied during commit (we apply immediately in this implementation but still log before apply to ensure atomicity).
*/

/* Transaction ID generator (timestamp based) */
void gen_txid(char *buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(buf, len, "TX%ld%ld", tv.tv_sec, tv.tv_usec);
}

/* Apply a simple action line of format:
   DEBIT acc_no amount
   CREDIT acc_no amount
   UPDATE_LOAN loan_id status
*/
int apply_action_line(const char *line) {
    char action[64];
    int id;
    double amount;
    if (sscanf(line, "%63s", action) != 1) return -1;
    if (strcmp(action, "DEBIT") == 0) {
        if (sscanf(line, "DEBIT %d %lf", &id, &amount) == 2) {
            Account acc;
            if (read_account_by_no(id, &acc) != sizeof(Account)) return -1;
            acc.balance -= amount;
            if (acc.balance < -1e-9) return -2; // invalid: negative after op
            write_account_by_no(id, &acc);
            return 0;
        }
    } else if (strcmp(action, "CREDIT") == 0) {
        if (sscanf(line, "CREDIT %d %lf", &id, &amount) == 2) {
            Account acc;
            if (read_account_by_no(id, &acc) != sizeof(Account)) return -1;
            acc.balance += amount;
            write_account_by_no(id, &acc);
            return 0;
        }
    } else if (strcmp(action, "UPDATE_LOAN") == 0) {
        int loan_id, status;
        if (sscanf(line, "UPDATE_LOAN %d %d", &loan_id, &status) == 2) {
            Loan loan;
            if (read_loan_by_id(loan_id, &loan) != sizeof(Loan)) return -1;
            loan.status = status;
            write_loan_by_id(loan_id, &loan);
            return 0;
        }
    }
    return -1;
}

/* WAL-based atomic transfer: debit from 'from', credit 'to' */
int tx_transfer(int from_acc, int to_acc, double amount) {
    if (from_acc == to_acc) return -1;
    // to avoid deadlocks, lock lower acc_no first
    int a = (from_acc < to_acc) ? from_acc : to_acc;
    int b = (from_acc < to_acc) ? to_acc : from_acc;

    off_t off_a = (off_t)(a - 1) * sizeof(Account);
    off_t off_b = (off_t)(b - 1) * sizeof(Account);
    // Lock both records for write
    if (lock_by_offset(fd_acc, off_a, sizeof(Account), F_WRLCK) < 0) return -1;
    if (lock_by_offset(fd_acc, off_b, sizeof(Account), F_WRLCK) < 0) {
        // unlock a
        lock_by_offset(fd_acc, off_a, sizeof(Account), F_UNLCK);
        return -1;
    }

    // Basic pre-checks
    Account acc_from, acc_to;
    if (read_account_by_no(from_acc, &acc_from) != sizeof(Account) ||
        read_account_by_no(to_acc, &acc_to) != sizeof(Account)) {
        lock_by_offset(fd_acc, off_a, sizeof(Account), F_UNLCK);
        lock_by_offset(fd_acc, off_b, sizeof(Account), F_UNLCK);
        return -1;
    }
    if (!acc_from.active || !acc_to.active) {
        lock_by_offset(fd_acc, off_a, sizeof(Account), F_UNLCK);
        lock_by_offset(fd_acc, off_b, sizeof(Account), F_UNLCK);
        return -1;
    }
    if (acc_from.balance + 1e-9 < amount) {
        // insufficient funds
        lock_by_offset(fd_acc, off_a, sizeof(Account), F_UNLCK);
        lock_by_offset(fd_acc, off_b, sizeof(Account), F_UNLCK);
        return -2;
    }

    // WAL: create tx
    char txid[64];
    gen_txid(txid, sizeof(txid));
    char buf[TX_BUF];

    snprintf(buf, sizeof(buf), "BEGIN %s", txid);
    wal_append_line(buf);

    snprintf(buf, sizeof(buf), "DEBIT %d %.2f", from_acc, amount);
    wal_append_line(buf);

    snprintf(buf, sizeof(buf), "CREDIT %d %.2f", to_acc, amount);
    wal_append_line(buf);

    // COMMIT
    snprintf(buf, sizeof(buf), "COMMIT %s", txid);
    wal_append_line(buf);

    // Apply: NOTE: here we apply after WAL ensures durability. In a production WAL/redo you'd apply before commit or use two-phase approach. For clarity we:
    // Apply debit then credit (both already locked).
    acc_from.balance -= amount;
    acc_to.balance += amount;
    write_account_by_no(from_acc, &acc_from);
    write_account_by_no(to_acc, &acc_to);
    fsync(fd_acc);

    // unlock
    lock_by_offset(fd_acc, off_a, sizeof(Account), F_UNLCK);
    lock_by_offset(fd_acc, off_b, sizeof(Account), F_UNLCK);
    return 0;
}

/* Deposit or withdraw (single account TX) */
int tx_deposit(int acc_no, double amount) {
    off_t off = (off_t)(acc_no - 1) * sizeof(Account);
    if (lock_by_offset(fd_acc, off, sizeof(Account), F_WRLCK) < 0) return -1;
    Account acc;
    if (read_account_by_no(acc_no, &acc) != sizeof(Account)) {
        lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK);
        return -1;
    }
    if (!acc.active) { lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK); return -1; }

    char txid[64], buf[TX_BUF];
    gen_txid(txid, sizeof(txid));
    snprintf(buf, sizeof(buf), "BEGIN %s", txid); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "CREDIT %d %.2f", acc_no, amount); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "COMMIT %s", txid); wal_append_line(buf);

    acc.balance += amount;
    write_account_by_no(acc_no, &acc);
    fsync(fd_acc);

    lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK);
    return 0;
}
int tx_withdraw(int acc_no, double amount) {
    off_t off = (off_t)(acc_no - 1) * sizeof(Account);
    if (lock_by_offset(fd_acc, off, sizeof(Account), F_WRLCK) < 0) return -1;
    Account acc;
    if (read_account_by_no(acc_no, &acc) != sizeof(Account)) {
        lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK);
        return -1;
    }
    if (!acc.active) { lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK); return -1; }
    if (acc.balance + 1e-9 < amount) {
        lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK);
        return -2;
    }

    char txid[64], buf[TX_BUF];
    gen_txid(txid, sizeof(txid));
    snprintf(buf, sizeof(buf), "BEGIN %s", txid); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "DEBIT %d %.2f", acc_no, amount); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "COMMIT %s", txid); wal_append_line(buf);

    acc.balance -= amount;
    write_account_by_no(acc_no, &acc);
    fsync(fd_acc);

    lock_by_offset(fd_acc, off, sizeof(Account), F_UNLCK);
    return 0;
}

/* Simple loan application: creates loan record in loans.dat with LOAN_PENDING */
int apply_loan(int acc_no, double amount, const char *purpose) {
    int loan_id = next_loan_id();
    Loan loan;
    memset(&loan, 0, sizeof(Loan));
    loan.loan_id = loan_id;
    loan.acc_no = acc_no;
    loan.amount = amount;
    loan.status = LOAN_PENDING;
    strncpy(loan.purpose, purpose, sizeof(loan.purpose)-1);
    write_loan_by_id(loan_id, &loan);
    return loan_id;
}

/* Manager approves/rejects: use WAL to record UPDATE_LOAN and apply */
int update_loan_status(int loan_id, int new_status) {
    // lock loan record for write
    off_t off = (off_t)(loan_id - 1) * sizeof(Loan);
    if (lock_by_offset(fd_loan, off, sizeof(Loan), F_WRLCK) < 0) return -1;
    Loan loan;
    if (read_loan_by_id(loan_id, &loan) != sizeof(Loan)) {
        lock_by_offset(fd_loan, off, sizeof(Loan), F_UNLCK);
        return -1;
    }
    // WAL entry
    char txid[64], buf[TX_BUF];
    gen_txid(txid, sizeof(txid));
    snprintf(buf, sizeof(buf), "BEGIN %s", txid); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "UPDATE_LOAN %d %d", loan_id, new_status); wal_append_line(buf);
    snprintf(buf, sizeof(buf), "COMMIT %s", txid); wal_append_line(buf);

    loan.status = new_status;
    write_loan_by_id(loan_id, &loan);
    fsync(fd_loan);

    lock_by_offset(fd_loan, off, sizeof(Loan), F_UNLCK);
    return 0;
}

/* WAL recovery on startup: parse wal.log and apply committed transactions */
void wal_recover_and_apply() {
    // Simple implementation: read wal file, collect transactions between BEGIN/COMMIT, only apply if COMMIT exists.
    lseek(fd_wal, 0, SEEK_SET);
    FILE *f = fdopen(dup(fd_wal), "r");
    if (!f) return;
    char line[1024];
    int in_tx = 0;
    char txid[128] = {0};
    // store actions temporarily in a dynamic list
    char **actions = NULL;
    size_t act_count = 0;

    while (fgets(line, sizeof(line), f)) {
        // strip newline
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "BEGIN ", 6) == 0) {
            in_tx = 1;
            strncpy(txid, line + 6, sizeof(txid)-1);
            // clear actions
            for (size_t i=0;i<act_count;i++) free(actions[i]);
            free(actions); actions = NULL; act_count = 0;
        } else if (in_tx && strncmp(line, "COMMIT ", 7) == 0) {
            // commit — apply actions
            for (size_t i=0;i<act_count;i++) {
                // apply action string
                // for safety, try parsing and applying; ignore errors (basic)
                apply_action_line(actions[i]);
            }
            in_tx = 0;
            txid[0] = 0;
            for (size_t i=0;i<act_count;i++) free(actions[i]);
            free(actions); actions = NULL; act_count = 0;
        } else {
            // action line (DEBIT/CREDIT/UPDATE_LOAN)
            if (in_tx) {
                actions = realloc(actions, sizeof(char*) * (act_count+1));
                actions[act_count] = strdup(line);
                act_count++;
            } else {
                // stray action without BEGIN; ignore
            }
        }
    }
    // cleanup
    for (size_t i=0;i<act_count;i++) free(actions[i]);
    free(actions);
    fclose(f);

    // Optionally truncate WAL after recovery (for demo we keep it)
}

/* Networking & protocol:
   Simple text protocol. Client logs in by sending:
   ROLE <role> (role: CUSTOMER/EMPLOYEE/MANAGER/ADMIN)
   AUTH <acc_no> <password>
   Then server will respond "OK" or "ERR ..." and then command loop.
   Commands vary by role, e.g.:
   CUSTOMER: DEPOSIT amount | WITHDRAW amount | TRANSFER to_acc amount | APPLY_LOAN amount purpose | VIEW | LOGOUT
   EMPLOYEE: LIST_PENDING_LOANS | MARK_REVIEW loan_id | LOGOUT
   MANAGER: LIST_REVIEWED | APPROVE loan_id | REJECT loan_id | LOGOUT
   ADMIN: CREATE_ACCOUNT name password balance role | DELETE_ACCOUNT acc_no | LIST_ACCOUNTS | LOGOUT
*/

/* Utility read/write line (socket) */
ssize_t sock_readline(int fd, char *buf, size_t max) {
    ssize_t total = 0;
    while (total < (ssize_t)max-1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return (total>0)? total : n;
        if (c == '\n') break;
        buf[total++] = c;
    }
    buf[total] = 0;
    return total;
}
ssize_t sock_writeline(int fd, const char *s) {
    size_t len = strlen(s);
    ssize_t n = write(fd, s, len);
    write(fd, "\n", 1);
    return n;
}

/* Simple trim of whitespace */
void trim(char *s) {
    size_t i = strlen(s);
    while (i>0 && (s[i-1]=='\n' || s[i-1]=='\r' || s[i-1]==' ' || s[i-1]=='\t')) { s[i-1]=0; i--; }
}

/* Handler for an authenticated client */
void handle_client_session(int client_fd, int role, int acc_no) {
    char buf[1024];
    char reply[1024];

    if (role == ROLE_CUSTOMER) {
        sock_writeline(client_fd, "Welcome, CUSTOMER. Commands: DEPOSIT amt | WITHDRAW amt | TRANSFER to amt | APPLY_LOAN amt purpose | VIEW | LOGOUT");
        while (1) {
            ssize_t n = sock_readline(client_fd, buf, sizeof(buf));
            if (n <= 0) break;
            trim(buf);
            if (strcasecmp(buf, "LOGOUT")==0) break;
            if (strncasecmp(buf, "DEPOSIT ", 8)==0) {
                double amt = atof(buf+8);
                int r = tx_deposit(acc_no, amt);
                if (r==0) sock_writeline(client_fd, "Deposit OK");
                else sock_writeline(client_fd, "Deposit Failed");
            } else if (strncasecmp(buf, "WITHDRAW ", 9)==0) {
                double amt = atof(buf+9);
                int r = tx_withdraw(acc_no, amt);
                if (r==0) sock_writeline(client_fd, "Withdraw OK");
                else if (r==-2) sock_writeline(client_fd, "Insufficient funds");
                else sock_writeline(client_fd, "Withdraw Failed");
            } else if (strncasecmp(buf, "TRANSFER ", 9)==0) {
                int to; double amt;
                if (sscanf(buf+9, "%d %lf", &to, &amt) == 2) {
                    int r = tx_transfer(acc_no, to, amt);
                    if (r==0) sock_writeline(client_fd, "Transfer OK");
                    else if (r==-2) sock_writeline(client_fd, "Insufficient funds or invalid");
                    else sock_writeline(client_fd, "Transfer Failed");
                } else sock_writeline(client_fd, "Bad format: TRANSFER to_acc amount");
            } else if (strncasecmp(buf, "APPLY_LOAN ", 11)==0) {
                double amt; char purpose[128];
                // expectation: APPLY_LOAN <amt> <purpose...>
                char *p = buf + 11;
                if (sscanf(p, "%lf", &amt) >= 1) {
                    // find first space after amount
                    char *space = strchr(p, ' ');
                    if (space) {
                        strncpy(purpose, space+1, sizeof(purpose)-1);
                    } else strcpy(purpose, "General");
                    int loan_id = apply_loan(acc_no, amt, purpose);
                    snprintf(reply, sizeof(reply), "Loan Applied. LoanID=%d", loan_id);
                    sock_writeline(client_fd, reply);
                } else sock_writeline(client_fd, "Bad format: APPLY_LOAN amount purpose");
            } else if (strcasecmp(buf, "VIEW")==0) {
                Account a;
                if (read_account_by_no(acc_no, &a) == sizeof(Account)) {
                    snprintf(reply, sizeof(reply), "Acc %d: Name=%s Balance=%.2f Active=%d", a.acc_no, a.name, a.balance, a.active);
                    sock_writeline(client_fd, reply);
                } else sock_writeline(client_fd, "Error reading account");
            } else sock_writeline(client_fd, "Unknown command");
        } // end while
    } else if (role == ROLE_EMPLOYEE) {
        // list pending loans
        sock_writeline(client_fd, "EMPLOYEE: Commands: LIST_PENDING | MARK_REVIEW loan_id | LOGOUT");
        while (1) {
            ssize_t n = sock_readline(client_fd, buf, sizeof(buf));
            if (n<=0) break;
            trim(buf);
            if (strcasecmp(buf, "LOGOUT")==0) break;
            if (strcasecmp(buf, "LIST_PENDING")==0) {
                // scan loans.dat
                struct stat st; fstat(fd_loan, &st);
                int cnt = st.st_size / sizeof(Loan);
                for (int i=1;i<=cnt;i++) {
                    Loan loan; if (read_loan_by_id(i, &loan) == sizeof(Loan)) {
                        if (loan.status == LOAN_PENDING) {
                            snprintf(reply, sizeof(reply), "LoanID=%d Acc=%d Amt=%.2f Purpose=%s", loan.loan_id, loan.acc_no, loan.amount, loan.purpose);
                            sock_writeline(client_fd, reply);
                        }
                    }
                }
                sock_writeline(client_fd, "END_LIST");
            } else if (strncasecmp(buf, "MARK_REVIEW ", 12)==0) {
                int lid = atoi(buf+12);
                // mark reviewed
                int r = update_loan_status(lid, LOAN_REVIEWED);
                if (r==0) sock_writeline(client_fd, "Marked REVIEWED");
                else sock_writeline(client_fd, "Failed to mark review");
            } else sock_writeline(client_fd, "Unknown command");
        }
    } else if (role == ROLE_MANAGER) {
        sock_writeline(client_fd, "MANAGER: Commands: LIST_REVIEWED | APPROVE loan_id | REJECT loan_id | LOGOUT");
        while (1) {
            ssize_t n = sock_readline(client_fd, buf, sizeof(buf));
            if (n<=0) break;
            trim(buf);
            if (strcasecmp(buf, "LOGOUT")==0) break;
            if (strcasecmp(buf, "LIST_REVIEWED")==0) {
                struct stat st; fstat(fd_loan, &st);
                int cnt = st.st_size / sizeof(Loan);
                for (int i=1;i<=cnt;i++) {
                    Loan loan; if (read_loan_by_id(i, &loan) == sizeof(Loan)) {
                        if (loan.status == LOAN_REVIEWED) {
                            snprintf(reply, sizeof(reply), "LoanID=%d Acc=%d Amt=%.2f Purpose=%s", loan.loan_id, loan.acc_no, loan.amount, loan.purpose);
                            sock_writeline(client_fd, reply);
                        }
                    }
                }
                sock_writeline(client_fd, "END_LIST");
            } else if (strncasecmp(buf, "APPROVE ", 8)==0) {
                int lid = atoi(buf+8);
                int r = update_loan_status(lid, LOAN_APPROVED);
                if (r==0) sock_writeline(client_fd, "Approved");
                else sock_writeline(client_fd, "Failed to approve");
            } else if (strncasecmp(buf, "REJECT ", 7)==0) {
                int lid = atoi(buf+7);
                int r = update_loan_status(lid, LOAN_REJECTED);
                if (r==0) sock_writeline(client_fd, "Rejected");
                else sock_writeline(client_fd, "Failed to reject");
            } else sock_writeline(client_fd, "Unknown command");
        }
    } else if (role == ROLE_ADMIN) {
        sock_writeline(client_fd, "ADMIN: Commands: CREATE_ACCOUNT name pwd bal role | DELETE_ACCOUNT acc_no | LIST_ACCOUNTS | LOGOUT");
        while (1) {
            ssize_t n = sock_readline(client_fd, buf, sizeof(buf));
            if (n<=0) break;
            trim(buf);
            if (strcasecmp(buf, "LOGOUT")==0) break;
            if (strncasecmp(buf, "CREATE_ACCOUNT ", 15)==0) {
                // format: CREATE_ACCOUNT name pwd balance rolenum
                char name[MAX_NAME], pwd[MAX_PASS];
                double bal; int rolenum;
                // naive parse:
                char *p = buf + 15;
                if (sscanf(p, "%63s %31s %lf %d", name, pwd, &bal, &rolenum) == 4) {
                    int accno = next_account_no();
                    Account a; memset(&a,0,sizeof(Account));
                    a.acc_no = accno; a.role = rolenum; strncpy(a.name, name, sizeof(a.name)-1);
                    strncpy(a.password, pwd, sizeof(a.password)-1); a.balance = bal; a.active = 1;
                    write_account_by_no(accno, &a);
                    snprintf(reply, sizeof(reply), "Created account %d", accno);
                    sock_writeline(client_fd, reply);
                } else sock_writeline(client_fd, "Bad format: CREATE_ACCOUNT name pwd balance role");
            } else if (strncasecmp(buf, "DELETE_ACCOUNT ", 15)==0) {
                int accdel = atoi(buf+15);
                Account a;
                if (read_account_by_no(accdel, &a) == sizeof(Account)) {
                    a.active = 0; write_account_by_no(accdel, &a);
                    sock_writeline(client_fd, "Deleted (marked inactive)");
                } else sock_writeline(client_fd, "Account not found");
            } else if (strcasecmp(buf, "LIST_ACCOUNTS")==0) {
                struct stat st; fstat(fd_acc, &st);
                int cnt = st.st_size / sizeof(Account);
                for (int i=1;i<=cnt;i++) {
                    Account a; if (read_account_by_no(i, &a) == sizeof(Account)) {
                        snprintf(reply, sizeof(reply), "Acc=%d Name=%s Role=%d Bal=%.2f Active=%d", a.acc_no, a.name, a.role, a.balance, a.active);
                        sock_writeline(client_fd, reply);
                    }
                }
                sock_writeline(client_fd, "END_LIST");
            } else sock_writeline(client_fd, "Unknown command");
        }
    }
    sock_writeline(client_fd, "BYE");
}

/* Authentication flow: simple. Client first sends e.g.:
   LOGIN <role> <acc_no> <password>
   role string: CUSTOMER / EMPLOYEE / MANAGER / ADMIN
*/
int parse_role_str(const char *s) {
    if (strcasecmp(s, "CUSTOMER")==0) return ROLE_CUSTOMER;
    if (strcasecmp(s, "EMPLOYEE")==0) return ROLE_EMPLOYEE;
    if (strcasecmp(s, "MANAGER")==0)  return ROLE_MANAGER;
    if (strcasecmp(s, "ADMIN")==0)    return ROLE_ADMIN;
    return -1;
}

/* Thread: accept client socket and handle login + session */
void *client_thread(void *arg) {
    int client_fd = *(int*)arg; free(arg);
    char buf[1024];
    // prompt
    sock_writeline(client_fd, "BANK_SERVER: Send LOGIN <ROLE> <acc_no> <password>");
    ssize_t n = sock_readline(client_fd, buf, sizeof(buf));
    if (n <= 0) { close(client_fd); return NULL; }
    trim(buf);
    // parse
    char cmd[16], role_s[32], pass[64];
    int acc_no = -1;
    if (sscanf(buf, "%15s %31s %d %63s", cmd, role_s, &acc_no, pass) != 4) {
        sock_writeline(client_fd, "ERR bad login format");
        close(client_fd); return NULL;
    }
    if (strcasecmp(cmd, "LOGIN") != 0) {
        sock_writeline(client_fd, "ERR expected LOGIN");
        close(client_fd); return NULL;
    }
    int role = parse_role_str(role_s);
    if (role < 0) { sock_writeline(client_fd, "ERR unknown role"); close(client_fd); return NULL; }

    if (role == ROLE_ADMIN) {
        if (strcmp(pass, ADMIN_PASS) == 0) {
            sock_writeline(client_fd, "OK admin");
            handle_client_session(client_fd, ROLE_ADMIN, 0);
        } else {
            sock_writeline(client_fd, "ERR admin auth");
        }
    } else {
        // validate user from accounts.dat
        Account acc;
        if (acc_no <= 0) { sock_writeline(client_fd, "ERR invalid account"); close(client_fd); return NULL; }
        if (read_account_by_no(acc_no, &acc) != sizeof(Account)) {
            sock_writeline(client_fd, "ERR no such account"); close(client_fd); return NULL;
        }
        if (!acc.active) { sock_writeline(client_fd, "ERR inactive account"); close(client_fd); return NULL; }
        if (strcmp(acc.password, pass) != 0) {
            sock_writeline(client_fd, "ERR bad password"); close(client_fd); return NULL;
        }
        // check role matches (employee/manager/customer)
        if (acc.role != role) {
            // allow some flexibility: a manager account may be used by admin, etc. For demo we enforce equal.
            sock_writeline(client_fd, "ERR role mismatch");
            close(client_fd); return NULL;
        }
        sock_writeline(client_fd, "OK auth");
        handle_client_session(client_fd, role, acc_no);
    }

    close(client_fd);
    return NULL;
}

/* Server main */
int main() {
    ensure_files();
    wal_recover_and_apply(); // recover before serving

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_addr.s_addr = INADDR_ANY; srv.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&srv, sizeof(srv)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); exit(1); }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        struct sockaddr_in cli; socklen_t len = sizeof(cli);
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, (struct sockaddr*)&cli, &len);
        if (*client_fd < 0) { free(client_fd); continue; }
        pthread_t tid; pthread_create(&tid, NULL, client_thread, client_fd);
        pthread_detach(tid);
    }

    close(fd_acc); close(fd_loan); close(fd_wal); close(listen_fd);
    return 0;
}
