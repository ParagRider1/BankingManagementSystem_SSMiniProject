// Banking server implementation


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "common.h"

#define PORT 8080
#define MAX_CLIENTS 10

//Part 1 — reading/writing accounts safely using system calls (open, read, write, lseek, and file locking).


int db_fd;  // File descriptor for database

// ------------------------------
// File helper functions
// ------------------------------

// Lock record by account number (read=F_RDLCK / write=F_WRLCK)
void lock_record(int fd, int acc_no, short lock_type) {
    struct flock lock;
    lock.l_type = lock_type;
    lock.l_whence = SEEK_SET;
    lock.l_start = (acc_no - 1) * sizeof(Account);
    lock.l_len = sizeof(Account);
    lock.l_pid = getpid();

    fcntl(fd, F_SETLKW, &lock); // block until lock acquired
}

// Unlock record
void unlock_record(int fd, int acc_no) {
    struct flock lock;
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (acc_no - 1) * sizeof(Account);
    lock.l_len = sizeof(Account);
    lock.l_pid = getpid();

    fcntl(fd, F_SETLK, &lock);
}

// Read one account from DB
int read_account(int acc_no, Account *acc) {
    lseek(db_fd, (acc_no - 1) * sizeof(Account), SEEK_SET);
    return read(db_fd, acc, sizeof(Account));
}

// Write one account to DB
int write_account(int acc_no, Account *acc) {
    lseek(db_fd, (acc_no - 1) * sizeof(Account), SEEK_SET);
    return write(db_fd, acc, sizeof(Account));
}

// Search for account by acc_no
int account_exists(int acc_no) {
    Account acc;
    if (read_account(acc_no, &acc) > 0 && acc.active)
        return 1;
    return 0;
}






//part 2--admin operation (add, delete, modify, search accounts).

// ------------------------------
// Admin operations
// ------------------------------

// Add new account
void admin_add_account(int client_fd) {
    Account acc = {0};
    char buf[MAX_CLIENT_MSG];

    write(client_fd, "Enter account type (1-Normal / 2-Joint): ", 42);
    read(client_fd, buf, sizeof(buf));
    acc.acc_type = atoi(buf);

    write(client_fd, "Enter name1: ", 13);
    read(client_fd, acc.name1, sizeof(acc.name1));

    if (acc.acc_type == ACC_JOINT) {
        write(client_fd, "Enter name2: ", 13);
        read(client_fd, acc.name2, sizeof(acc.name2));
    } else {
        strcpy(acc.name2, "");
    }

    write(client_fd, "Enter password: ", 16);
    read(client_fd, acc.password, sizeof(acc.password));

    write(client_fd, "Enter initial balance: ", 23);
    read(client_fd, buf, sizeof(buf));
    acc.balance = atof(buf);
    acc.active = 1;

    // Determine account number = (file size / struct size) + 1
    off_t pos = lseek(db_fd, 0, SEEK_END);
    acc.acc_no = (pos / sizeof(Account)) + 1;

    write_account(acc.acc_no, &acc);

    sprintf(buf, "Account created successfully. Account No: %d\n", acc.acc_no);
    write(client_fd, buf, strlen(buf));
}

// Delete (deactivate) account
void admin_delete_account(int client_fd) {
    char buf[MAX_CLIENT_MSG];
    write(client_fd, "Enter account number to delete: ", 32);
    read(client_fd, buf, sizeof(buf));
    int acc_no = atoi(buf);

    if (!account_exists(acc_no)) {
        write(client_fd, "Account not found.\n", 19);
        return;
    }

    lock_record(db_fd, acc_no, F_WRLCK);
    Account acc;
    read_account(acc_no, &acc);
    acc.active = 0;
    write_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    write(client_fd, "Account deleted (deactivated).\n", 31);
}

// Modify password
void admin_modify_account(int client_fd) {
    char buf[MAX_CLIENT_MSG];
    write(client_fd, "Enter account number to modify: ", 32);
    read(client_fd, buf, sizeof(buf));
    int acc_no = atoi(buf);

    if (!account_exists(acc_no)) {
        write(client_fd, "Account not found.\n", 19);
        return;
    }

    lock_record(db_fd, acc_no, F_WRLCK);
    Account acc;
    read_account(acc_no, &acc);

    write(client_fd, "Enter new password: ", 20);
    read(client_fd, acc.password, sizeof(acc.password));

    write_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    write(client_fd, "Password updated successfully.\n", 31);
}

// Search account details
void admin_search_account(int client_fd) {
    char buf[MAX_CLIENT_MSG];
    write(client_fd, "Enter account number to search: ", 32);
    read(client_fd, buf, sizeof(buf));
    int acc_no = atoi(buf);

    if (!account_exists(acc_no)) {
        write(client_fd, "Account not found.\n", 19);
        return;
    }

    lock_record(db_fd, acc_no, F_RDLCK);
    Account acc;
    read_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    sprintf(buf, "Account No: %d\nType: %s\nName1: %s\nName2: %s\nBalance: %.2f\nStatus: %s\n",
            acc.acc_no,
            acc.acc_type == ACC_JOINT ? "Joint" : "Normal",
            acc.name1,
            acc.name2,
            acc.balance,
            acc.active ? "Active" : "Inactive");
    write(client_fd, buf, strlen(buf));
}
// ------------------------------



//Part 3 :  which handles normal/joint user operations — deposit, withdraw, balance check, password change, and viewing details — using file locks to ensure data safety when multiple clients act concurrently.


// ------------------------------
// User and Joint Account Operations
// ------------------------------

void user_deposit(int client_fd, int acc_no) {
    char buf[MAX_CLIENT_MSG];
    double amount;

    write(client_fd, "Enter amount to deposit: ", 25);
    read(client_fd, buf, sizeof(buf));
    amount = atof(buf);

    lock_record(db_fd, acc_no, F_WRLCK);
    Account acc;
    read_account(acc_no, &acc);
    acc.balance += amount;
    write_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    write(client_fd, "Deposit successful.\n", 20);
}

void user_withdraw(int client_fd, int acc_no) {
    char buf[MAX_CLIENT_MSG];
    double amount;

    write(client_fd, "Enter amount to withdraw: ", 26);
    read(client_fd, buf, sizeof(buf));
    amount = atof(buf);

    lock_record(db_fd, acc_no, F_WRLCK);
    Account acc;
    read_account(acc_no, &acc);

    if (acc.balance >= amount) {
        acc.balance -= amount;
        write_account(acc_no, &acc);
        write(client_fd, "Withdrawal successful.\n", 23);
    } else {
        write(client_fd, "Insufficient balance.\n", 22);
    }

    unlock_record(db_fd, acc_no);
}

void user_balance(int client_fd, int acc_no) {
    lock_record(db_fd, acc_no, F_RDLCK);
    Account acc;
    read_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    char buf[MAX_CLIENT_MSG];
    sprintf(buf, "Current Balance: %.2f\n", acc.balance);
    write(client_fd, buf, strlen(buf));
}

void user_change_password(int client_fd, int acc_no) {
    char buf[MAX_CLIENT_MSG];
    Account acc;

    write(client_fd, "Enter new password: ", 20);
    read(client_fd, buf, sizeof(buf));

    lock_record(db_fd, acc_no, F_WRLCK);
    read_account(acc_no, &acc);
    strncpy(acc.password, buf, MAX_PASS);
    write_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    write(client_fd, "Password changed successfully.\n", 32);
}

void user_view_details(int client_fd, int acc_no) {
    lock_record(db_fd, acc_no, F_RDLCK);
    Account acc;
    read_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    char buf[MAX_CLIENT_MSG];
    sprintf(buf,
            "Account No: %d\nType: %s\nName1: %s\nName2: %s\nBalance: %.2f\nStatus: %s\n",
            acc.acc_no,
            acc.acc_type == ACC_JOINT ? "Joint" : "Normal",
            acc.name1,
            acc.name2,
            acc.balance,
            acc.active ? "Active" : "Inactive");
    write(client_fd, buf, strlen(buf));
}



//NOTE:
/*
Each user operation reads/writes to the same binary file (accounts.dat).

File locks (F_RDLCK or F_WRLCK) protect concurrent access.
Joint account users benefit most from this — two clients can view (read locks), but only one can update (write lock).

After each transaction, the data is written back to file immediately.
*/

//------------------------------------------------------------------------------------------




//Part 4:
// Login authentication
// Menu handling for admin and users
// Multithreaded client handling
// Server main function setup with sockets(socket programming)




// ------------------------------
// Authentication and Menus
// ------------------------------

// Admin menu
void admin_menu(int client_fd) {
    char buf[MAX_CLIENT_MSG];
    while (1) {
        char *menu = "\nAdmin Menu:\n"
                     "1. Add Account\n"
                     "2. Delete Account\n"
                     "3. Modify Account (Password)\n"
                     "4. Search Account\n"
                     "5. Logout\nChoice: ";
        write(client_fd, menu, strlen(menu));

        read(client_fd, buf, sizeof(buf));
        int choice = atoi(buf);

        switch (choice) {
            case 1: admin_add_account(client_fd); break;
            case 2: admin_delete_account(client_fd); break;
            case 3: admin_modify_account(client_fd); break;
            case 4: admin_search_account(client_fd); break;
            case 5: write(client_fd, "Logging out...\n", 15); return;
            default: write(client_fd, "Invalid choice.\n", 16);
        }
    }
}

// User menu
void user_menu(int client_fd, int acc_no) {
    char buf[MAX_CLIENT_MSG];
    while (1) {
        char *menu = "\nUser Menu:\n"
                     "1. Deposit\n"
                     "2. Withdraw\n"
                     "3. Balance Enquiry\n"
                     "4. Password Change\n"
                     "5. View Details\n"
                     "6. Logout\nChoice: ";
        write(client_fd, menu, strlen(menu));

        read(client_fd, buf, sizeof(buf));
        int choice = atoi(buf);

        switch (choice) {
            case 1: user_deposit(client_fd, acc_no); break;
            case 2: user_withdraw(client_fd, acc_no); break;
            case 3: user_balance(client_fd, acc_no); break;
            case 4: user_change_password(client_fd, acc_no); break;
            case 5: user_view_details(client_fd, acc_no); break;
            case 6: write(client_fd, "Logging out...\n", 15); return;
            default: write(client_fd, "Invalid choice.\n", 16);
        }
    }
}

// Authentication
int authenticate_user(int client_fd, Account *out_acc) {
    char buf[MAX_CLIENT_MSG];
    write(client_fd, "Enter Account Number (0 for Admin): ", 36);
    read(client_fd, buf, sizeof(buf));
    int acc_no = atoi(buf);

    write(client_fd, "Enter Password: ", 16);
    read(client_fd, buf, sizeof(buf));

    if (acc_no == 0) {
        // admin login
        if (strcmp(buf, ADMIN_PASS) == 0) {
            out_acc->acc_type = ACC_ADMIN;
            return 1;
        } else {
            write(client_fd, "Invalid admin password.\n", 24);
            return 0;
        }
    }

    if (!account_exists(acc_no)) {
        write(client_fd, "Account not found.\n", 19);
        return 0;
    }

    lock_record(db_fd, acc_no, F_RDLCK);
    Account acc;
    read_account(acc_no, &acc);
    unlock_record(db_fd, acc_no);

    if (strcmp(acc.password, buf) == 0 && acc.active) {
        *out_acc = acc;
        return 1;
    }

    write(client_fd, "Invalid credentials.\n", 21);
    return 0;
}

// ------------------------------
// Client handler thread
// ------------------------------
void *client_handler(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    Account acc;

    if (!authenticate_user(client_fd, &acc)) {
        close(client_fd);
        pthread_exit(NULL);
    }

    if (acc.acc_type == ACC_ADMIN)
        admin_menu(client_fd);
    else
        user_menu(client_fd, acc.acc_no);

    close(client_fd);
    pthread_exit(NULL);
}

// ------------------------------
// Server Main
// ------------------------------
int main() {
    struct sockaddr_in server_addr, client_addr;
    int server_fd, *client_fd_ptr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t tid;

    // Ensure data folder exists
    system("mkdir -p data");

    // Open database file (create if not exist)
    db_fd = open(DB_FILE, O_RDWR | O_CREAT, 0666);
    if (db_fd < 0) {
        perror("DB open failed");
        exit(1);
    }

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    listen(server_fd, MAX_CLIENTS);
    printf("Server started on port %d...\n", PORT);

    // Accept multiple clients concurrently
    while (1) {
        client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (*client_fd_ptr < 0) {
            perror("Accept failed");
            free(client_fd_ptr);
            continue;
        }

        pthread_create(&tid, NULL, client_handler, client_fd_ptr);
        pthread_detach(tid);
    }

    close(server_fd);
    close(db_fd);
    return 0;
}



// ---------------------------------------------------------------------------
// Note:
/* What This Does:

Admin login: Enter Account Number = 0, Password = admin123

User login: Enter valid account number and password stored in file

Each client runs in its own thread via pthread_create

Server handles multiple clients using sockets on port 8080

File locking ensures no race conditions for joint accounts
*/

