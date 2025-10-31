#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdbool.h>
#include "common.h"

//note initial : admin username: admin123 password: 1234

// Forward declarations for handlers
void handle_customer(int sock, Account *acc);
void handle_employee(int sock, Account *acc);
void handle_manager(int sock, Account *acc);
void handle_admin(int sock, Account *acc);


#define PORT 8080
#define DATA_FILE "accounts.dat"

// typedef struct {
//     int id;
//     char username[50];
//     char password[50];
//     char role[20];       // CUSTOMER, EMPLOYEE, MANAGER, ADMIN
//     float balance;
//     int loan_pending;    // 0 = none, 1 = requested, 2 = approved
// } Account;


// Helper to send full message safely to socket (with newline support)
void send_msg(int sock, const char *msg) {
    size_t len = strlen(msg);
    ssize_t sent = 0;
    while (sent < (ssize_t)len) {
        ssize_t n = write(sock, msg + sent, len - sent);
        if (n <= 0) break;
        sent += n;
    }
}


pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;


int find_account_by_username(const char *username, Account *acc) {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) return 0;
    while (fread(acc, sizeof(Account), 1, fp)) {
        if (strcmp(acc->username, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// Validate username, password, and role
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "common.h"

bool check_credentials(const char *username, const char *password, const char *role, Account *acc) {
     FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) {
        perror("Error opening accounts data file");
        return false;
    }
    if (!fp) {
        perror("Error opening accounts.db");
        return false;
    }

    Account a;
    while (fread(&a, sizeof(Account), 1, fp) == 1) {
        if (strcmp(a.username, username) == 0 &&
            strcmp(a.password, password) == 0 &&
            strcmp(a.role, role) == 0) {
            *acc = a;
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}


void *client_handler(void *arg);

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen"); exit(1);
    }

    printf("Server started on port %d...\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) { perror("accept"); continue; }

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, (void *)(intptr_t)client_fd);
        pthread_detach(tid);
    }
    close(server_fd);
    return 0;
}

void *client_handler(void *arg) {
    int sock = (intptr_t)arg;
    char buf[1024];
    Account acc;
    char username[50], password[50], role[20];

    // --- Step 1: Ask for role selection first ---
    const char *role_menu =
        "Select role:\n"
        "1. CUSTOMER\n"
        "2. EMPLOYEE\n"
        "3. MANAGER\n"
        "4. ADMIN\n"
        "Enter choice:\n";
    send_msg(sock, role_menu);

    int n = read(sock, buf, sizeof(buf) - 1);
    if (n <= 0) { close(sock); return NULL; }
    buf[n] = '\0';
    int choice = atoi(buf);

    switch (choice) {
        case 1: strcpy(role, "CUSTOMER"); break;
        case 2: strcpy(role, "EMPLOYEE"); break;
        case 3: strcpy(role, "MANAGER"); break;
        case 4: strcpy(role, "ADMIN"); break;
        default:
            send_msg(sock, "Invalid role choice. Connection closing.\n");
            close(sock);
            return NULL;
    }

    // --- Step 2: Ask username and password ---
    send_msg(sock, "Enter username:\n");
    n = read(sock, buf, sizeof(buf) - 1);
    if (n <= 0) { close(sock); return NULL; }
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = 0;
    strcpy(username, buf);

    send_msg(sock, "Enter password:\n");
    n = read(sock, buf, sizeof(buf) - 1);
    if (n <= 0) { close(sock); return NULL; }
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = 0;
    strcpy(password, buf);

    // --- Step 3: Verify credentials ---
    if (!check_credentials(username, password, role, &acc)) {
        send_msg(sock, "Invalid credentials or role.\nConnection closed.\n");
        close(sock);
        return NULL;
    }

    // --- Step 4: Login success ---
    send_msg(sock, "Login successful!\n");
    char role_msg[64];
    sprintf(role_msg, "ROLE:%s\n", acc.role);
    send_msg(sock, role_msg);

    // --- Step 5: Role-specific handler ---
    if (strcmp(acc.role, "CUSTOMER") == 0) {
    send_msg(sock, "MENU\n");
    handle_customer(sock, &acc);
} else if (strcmp(acc.role, "EMPLOYEE") == 0) {
    send_msg(sock, "MENU\n");
    handle_employee(sock, &acc);
} else if (strcmp(acc.role, "MANAGER") == 0) {
    send_msg(sock, "MENU\n");
    handle_manager(sock, &acc);
} else if (strcmp(acc.role, "ADMIN") == 0) {
    send_msg(sock, "MENU\n");
    handle_admin(sock, &acc);
}


    close(sock);
    return NULL;
}




// ---------------- CUSTOMER ROLE ----------------

void update_account(Account *acc) {
    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(DATA_FILE, "r+b");
    if (!fp) { pthread_mutex_unlock(&file_mutex); return; }
    Account tmp;
    while (fread(&tmp, sizeof(Account), 1, fp)) {
        if (tmp.id == acc->id) {
            fseek(fp, -sizeof(Account), SEEK_CUR);
            fwrite(acc, sizeof(Account), 1, fp);
            fflush(fp);
            break;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&file_mutex);
}

void handle_customer(int sock, Account *acc) {
    char buf[1024];
    while (1) {
        ssize_t n = read(sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;

        if (strcmp(buf, "DEPOSIT") == 0) {
            read(sock, buf, sizeof(buf));
            float amt = atof(buf);
            int fd = open(DATA_FILE, O_RDWR);
            flock(fd, LOCK_EX);             // write lock
            acc->balance += amt;
            update_account(acc);
            flock(fd, LOCK_UN);
            close(fd);
            send_msg(sock, "Deposit successful.");
        }

        else if (strcmp(buf, "WITHDRAW") == 0) {
            read(sock, buf, sizeof(buf));
            float amt = atof(buf);
            int fd = open(DATA_FILE, O_RDWR);
            flock(fd, LOCK_EX);
            if (acc->balance >= amt) {
                acc->balance -= amt;
                update_account(acc);
                send_msg(sock, "Withdrawal successful.");
            } else {
                send_msg(sock, "Insufficient balance.");
            }
            flock(fd, LOCK_UN);
            close(fd);
        }

        else if (strcmp(buf, "BALANCE") == 0) {
            char msg[100];
            snprintf(msg, sizeof(msg), "Current Balance: ₹%.2f", acc->balance);
            send_msg(sock, msg);
        }

        else if (strcmp(buf, "APPLY_LOAN") == 0) {
            if (acc->loan_pending == 0) {
                acc->loan_pending = 1;
                update_account(acc);
                send_msg(sock, "Loan request submitted for review.");
            } else {
                send_msg(sock, "Loan already pending or approved.");
            }
        }

        else if (strcmp(buf, "VIEW") == 0) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Account ID: %d\nUsername: %s\nBalance: ₹%.2f\nLoan Status: %s",
                     acc->id, acc->username, acc->balance,
                     acc->loan_pending == 0 ? "None" :
                     acc->loan_pending == 1 ? "Pending" : "Approved");
            send_msg(sock, msg);
        }

        else if (strcmp(buf, "LOGOUT") == 0) {
            send_msg(sock, "Logging out...");
            break;
        }

        else {
            send_msg(sock, "Invalid customer command.");
        }
    }
}

// Find account by numeric id (returns 1 if found and fills acc_out, 0 otherwise)
int find_account_by_id(int id, Account *acc_out) {
    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) { pthread_mutex_unlock(&file_mutex); return 0; }
    Account tmp;
    int found = 0;
    while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
        if (tmp.id == id) {
            if (acc_out) *acc_out = tmp;
            found = 1;
            break;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&file_mutex);
    return found;
}

// Credit an account by id (uses file lock and update_account)
int credit_account_by_id(int id, double amount) {
    // load account, lock and update
    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(DATA_FILE, "r+b");
    if (!fp) { pthread_mutex_unlock(&file_mutex); return -1; }
    Account tmp;
    int success = 0;
    while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
        if (tmp.id == id) {
            // Move back and update
            tmp.balance += amount;
            fseek(fp, -sizeof(Account), SEEK_CUR);
            fwrite(&tmp, sizeof(Account), 1, fp);
            fflush(fp);
            success = 1;
            break;
        }
    }
    fclose(fp);
    pthread_mutex_unlock(&file_mutex);
    return success ? 0 : -1;
}


// ---------------- EMPLOYEE ROLE ----------------
void handle_employee(int sock, Account *self) {
    // self is the employee account object (not used heavily here)
    char buf[1024];
    while (1) {
        ssize_t n = read(sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;

        // Commands expected from client (menu-driven client can send):
        // "VIEW_PENDING"      -> list accounts with loan_pending == 1
        // "MARK_REVIEW"       -> next line: account id to mark as reviewed (set loan_pending=2)
        // "VIEW_ACCOUNT"      -> next line: account id to display
        // "LOGOUT"

        if (strcmp(buf, "VIEW_PENDING") == 0) {
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "rb");
            if (!fp) {
                pthread_mutex_unlock(&file_mutex);
                send_msg(sock, "Failed to open accounts file.");
                continue;
            }
            Account tmp;
            int any = 0;
            while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
                if (tmp.loan_pending == 1) {
                    char out[256];
                    snprintf(out, sizeof(out), "AccID=%d Name=%s Balance=%.2f", tmp.id, tmp.username, tmp.balance);
                    send_msg(sock, out);
                    any = 1;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            if (!any) send_msg(sock, "No pending loans found.");
        }
        else if (strcmp(buf, "MARK_REVIEW") == 0) {
            // expect account id on next line
            if (read(sock, buf, sizeof(buf)) <= 0) break;
            buf[strcspn(buf, "\r\n")] = 0;
            int id = atoi(buf);
            Account target;
            if (!find_account_by_id(id, &target)) {
                send_msg(sock, "Account not found.");
                continue;
            }
            // lock file and update target.loan_pending -> 2 (reviewed)
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "r+b");
            if (!fp) { pthread_mutex_unlock(&file_mutex); send_msg(sock, "Failed to open file."); continue; }
            Account tmp;
            int done = 0;
            while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
                if (tmp.id == id) {
                    tmp.loan_pending = 2; // reviewed
                    fseek(fp, -sizeof(Account), SEEK_CUR);
                    fwrite(&tmp, sizeof(Account), 1, fp);
                    fflush(fp);
                    done = 1;
                    break;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            if (done) send_msg(sock, "Marked loan as REVIEWED (forwarded to manager).");
            else send_msg(sock, "Failed to update account.");
        }
        else if (strcmp(buf, "VIEW_ACCOUNT") == 0) {
            // expect account id on next line
            if (read(sock, buf, sizeof(buf)) <= 0) break;
            buf[strcspn(buf, "\r\n")] = 0;
            int id = atoi(buf);
            Account t;
            if (!find_account_by_id(id, &t)) {
                send_msg(sock, "Account not found.");
            } else {
                char out[512];
                snprintf(out, sizeof(out), "AccID=%d Name=%s Role=%s Balance=%.2f LoanStatus=%d",
                         t.id, t.username, t.role, t.balance, t.loan_pending);
                send_msg(sock, out);
            }
        }
        else if (strcmp(buf, "LOGOUT") == 0) {
            send_msg(sock, "Logging out.");
            break;
        }
        else {
            send_msg(sock, "Unknown employee command.");
        }
    } // end while
}


// ---------------- MANAGER ROLE ----------------
void handle_manager(int sock, Account *self) {
    char buf[1024];
    while (1) {
        ssize_t n = read(sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;

        // Commands:
        // "LIST_REVIEWED"  -> list accounts with loan_pending == 2
        // "APPROVE"        -> next line: account id to approve (set loan_pending=3 and credit amount)
        // "REJECT"         -> next line: account id to reject (set loan_pending=4)
        // "LOGOUT"

        if (strcmp(buf, "LIST_REVIEWED") == 0) {
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "rb");
            if (!fp) { pthread_mutex_unlock(&file_mutex); send_msg(sock, "Failed to open file."); continue; }
            Account tmp; int any = 0;
            while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
                if (tmp.loan_pending == 2) {
                    char out[256];
                    snprintf(out, sizeof(out), "AccID=%d Name=%s Balance=%.2f", tmp.id, tmp.username, tmp.balance);
                    send_msg(sock, out); any = 1;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            if (!any) send_msg(sock, "No reviewed loans found.");
        }
        else if (strcmp(buf, "APPROVE") == 0) {
            if (read(sock, buf, sizeof(buf)) <= 0) break;
            buf[strcspn(buf, "\r\n")] = 0;
            int id = atoi(buf);
            Account t;
            if (!find_account_by_id(id, &t)) { send_msg(sock, "Account not found."); continue; }
            // For demo we don't have loan amount stored; assume a fixed loan amount or
            // use e.g. a separate 'requested_amount' in account — but since we used acc->loan_pending only,
            // we'll credit a demo amount, e.g., 1000.0 (you can change to real amount if stored).
            double credit_amt = 1000.0;

            // update account: set loan_pending=3 (approved) and credit amount
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "r+b");
            if (!fp) { pthread_mutex_unlock(&file_mutex); send_msg(sock, "Failed to open file."); continue; }
            Account tmp; int done = 0;
            while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
                if (tmp.id == id) {
                    tmp.loan_pending = 3;
                    tmp.balance += credit_amt;
                    fseek(fp, -sizeof(Account), SEEK_CUR);
                    fwrite(&tmp, sizeof(Account), 1, fp);
                    fflush(fp);
                    done = 1;
                    break;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            if (done) {
                char out[128]; snprintf(out, sizeof(out), "Loan approved and ₹%.2f credited to account %d", credit_amt, id);
                send_msg(sock, out);
            } else send_msg(sock, "Failed to approve loan.");
        }
        else if (strcmp(buf, "REJECT") == 0) {
            if (read(sock, buf, sizeof(buf)) <= 0) break;
            buf[strcspn(buf, "\r\n")] = 0;
            int id = atoi(buf);
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "r+b");
            if (!fp) { pthread_mutex_unlock(&file_mutex); send_msg(sock, "Failed to open file."); continue; }
            Account tmp; int done = 0;
            while (fread(&tmp, sizeof(Account), 1, fp) == 1) {
                if (tmp.id == id) {
                    tmp.loan_pending = 4; // rejected
                    fseek(fp, -sizeof(Account), SEEK_CUR);
                    fwrite(&tmp, sizeof(Account), 1, fp);
                    fflush(fp);
                    done = 1; break;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            if (done) send_msg(sock, "Loan rejected.");
            else send_msg(sock, "Failed to reject loan.");
        }
        else if (strcmp(buf, "LOGOUT") == 0) {
            send_msg(sock, "Logging out.");
            break;
        }
        else send_msg(sock, "Unknown manager command.");
    }
}



// ---------------- ADMINISTRATOR ROLE ----------------

void handle_admin(int sock, Account *acc) {
    char buf[1024];

    //  Send the admin menu when login is successful
    send_msg(sock,
        "ROLE:ADMIN\n"
        "MENU:\n"
        "1. ADD_ACCOUNT\n"
        "2. DELETE_ACCOUNT\n"
        "3. MODIFY_ACCOUNT\n"
        "4. SEARCH_ACCOUNT\n"
        "5. VIEW_ALL\n"
        "6. LOGOUT\n"
        "Enter your command (e.g., ADD_ACCOUNT):"
    );

    while (1) {
        ssize_t n = read(sock, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;

        if (strcmp(buf, "ADD_ACCOUNT") == 0) {
            Account newAcc;
            send_msg(sock, "Enter ID:");
            read(sock, buf, sizeof(buf)); newAcc.id = atoi(buf);

            send_msg(sock, "Enter Username:");
            read(sock, newAcc.username, sizeof(newAcc.username));
            newAcc.username[strcspn(newAcc.username, "\r\n")] = 0;

            send_msg(sock, "Enter Password:");
            read(sock, newAcc.password, sizeof(newAcc.password));
            newAcc.password[strcspn(newAcc.password, "\r\n")] = 0;

            send_msg(sock, "Enter Role (CUSTOMER/EMPLOYEE/MANAGER):");
            read(sock, newAcc.role, sizeof(newAcc.role));
            newAcc.role[strcspn(newAcc.role, "\r\n")] = 0;

            newAcc.balance = 0;
            newAcc.loan_pending = 0;

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "ab");
            fwrite(&newAcc, sizeof(Account), 1, fp);
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);

            send_msg(sock, "Account added successfully.");
        }

        else if (strcmp(buf, "DELETE_ACCOUNT") == 0) {
            send_msg(sock, "Enter Account ID to delete:");
            read(sock, buf, sizeof(buf));
            int delId = atoi(buf);

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "rb");
            FILE *temp = fopen("temp.dat", "wb");
            Account tmp; int found = 0;
            while (fread(&tmp, sizeof(Account), 1, fp)) {
                if (tmp.id == delId) found = 1;
                else fwrite(&tmp, sizeof(tmp), 1, temp);
            }
            fclose(fp); fclose(temp);
            remove(DATA_FILE); rename("temp.dat", DATA_FILE);
            pthread_mutex_unlock(&file_mutex);

            send_msg(sock, found ? "Account deleted." : "Account not found.");
        }

        else if (strcmp(buf, "MODIFY_ACCOUNT") == 0) {
            send_msg(sock, "Enter Account ID to modify:");
            read(sock, buf, sizeof(buf));
            int id = atoi(buf);

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "r+b");
            Account tmp; int found = 0;
            while (fread(&tmp, sizeof(Account), 1, fp)) {
                if (tmp.id == id) {
                    found = 1;
                    send_msg(sock, "Enter new password:");
                    read(sock, tmp.password, sizeof(tmp.password));
                    tmp.password[strcspn(tmp.password, "\r\n")] = 0;
                    fseek(fp, -sizeof(Account), SEEK_CUR);
                    fwrite(&tmp, sizeof(Account), 1, fp);
                    break;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);

            send_msg(sock, found ? "Account updated." : "Account not found.");
        }

        else if (strcmp(buf, "SEARCH_ACCOUNT") == 0) {
            send_msg(sock, "Enter Account ID to search:");
            read(sock, buf, sizeof(buf));
            int id = atoi(buf);

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "rb");
            Account tmp; int found = 0;
            while (fread(&tmp, sizeof(Account), 1, fp)) {
                if (tmp.id == id) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "Account ID: %d\nUser: %s\nRole: %s\nBalance: ₹%.2f\nLoan: %s",
                             tmp.id, tmp.username, tmp.role, tmp.balance,
                             tmp.loan_pending ? "Pending/Approved" : "None");
                    send_msg(sock, msg);
                    found = 1;
                    break;
                }
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);

            if (!found) send_msg(sock, "Account not found.");
        }

        else if (strcmp(buf, "VIEW_ALL") == 0) {
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATA_FILE, "rb");
            Account tmp;
            char msg[1024] = "";
            while (fread(&tmp, sizeof(Account), 1, fp)) {
                char line[256];
                snprintf(line, sizeof(line), "ID:%d User:%s Role:%s Bal:₹%.2f Loan:%d\n",
                         tmp.id, tmp.username, tmp.role, tmp.balance, tmp.loan_pending);
                strcat(msg, line);
            }
            fclose(fp);
            pthread_mutex_unlock(&file_mutex);
            send_msg(sock, msg[0] ? msg : "No accounts found.");
        }

        else if (strcmp(buf, "LOGOUT") == 0) {
            send_msg(sock, "Logging out...");
            break;
        }

        else {
            send_msg(sock, "Invalid admin command. Please choose from the menu.");
        }

        // ✅ Re-show menu after each command
        send_msg(sock,
            "\nMENU:\n"
            "1. ADD_ACCOUNT\n"
            "2. DELETE_ACCOUNT\n"
            "3. MODIFY_ACCOUNT\n"
            "4. SEARCH_ACCOUNT\n"
            "5. VIEW_ALL\n"
            "6. LOGOUT\n"
            "Enter your command:"
        );
    }
}



