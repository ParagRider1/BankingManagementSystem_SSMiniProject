#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080

//note initial : admin username: admin123 password: 1234

ssize_t readln(int fd, char *buf, size_t max) {
    ssize_t t = 0;
    while (t < (ssize_t)max - 1) {
        char c; ssize_t n = read(fd, &c, 1);
        if (n <= 0) return (t > 0) ? t : n;
        if (c == '\n') break;
        buf[t++] = c;
    }
    buf[t] = 0;
    return t;
}

void show_customer_menu() {
    printf("\n====== CUSTOMER MENU ======\n");
    printf("1. Deposit\n2. Withdraw\n3. Balance Enquiry\n4. Apply Loan\n5. View Details\n6. Logout\n");
    printf("============================\nEnter choice: ");
}

void show_employee_menu() {
    printf("\n====== EMPLOYEE MENU ======\n");
    printf("1. View Customer Details\n2. Process Loan Request\n3. Logout\n");
    printf("============================\nEnter choice: ");
}

void show_manager_menu() {
    printf("\n====== MANAGER MENU ======\n");
    printf("1. Approve/Reject Loan\n2. View All Accounts\n3. Logout\n");
    printf("===========================\nEnter choice: ");
}

void show_admin_menu() {
    printf("\n====== ADMIN MENU ======\n");
    printf("1. Add Account\n2. Delete Account\n3. Modify Account\n4. Search Account\n5. View All Accounts\n6. Logout\n");
    printf("=========================\nEnter choice: ");
}

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

    if (connect(s, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        return 1;
    }

    printf("Connected to server %s:%d\n", SERVER_IP, PORT);

    char buf[2048], role[32] = "";
    while (1) {
        // read full message (multi-line capable)
        ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("Server disconnected.\n");
            break;
        }
        buf[n] = '\0';

        // detect role update
        if (strncmp(buf, "ROLE:", 5) == 0) {
            strcpy(role, buf + 5);
            printf("\nLogged in as %s\n", role);
            continue;
        }

        // detect menu trigger
        if (strncmp(buf, "MENU", 4) == 0) {
            if (strcmp(role, "CUSTOMER") == 0) show_customer_menu();
            else if (strcmp(role, "EMPLOYEE") == 0) show_employee_menu();
            else if (strcmp(role, "MANAGER") == 0) show_manager_menu();
            else if (strcmp(role, "ADMIN") == 0) show_admin_menu();
            continue;
        }

        // print server message cleanly
        printf("%s", buf);
        if (buf[strlen(buf) - 1] != '\n') printf("\n");

        // exit conditions
        if (strstr(buf, "Logging out") || strstr(buf, "Goodbye")) break;

        // wait for user input
        printf("> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) break;
        buf[strcspn(buf, "\n")] = 0;

        // send input
        send(s, buf, strlen(buf), 0);

        // handle client-side menu logic
        int choice = atoi(buf);
        if (strcmp(role, "CUSTOMER") == 0) {
            switch (choice) {
                case 1: send(s, "DEPOSIT\n", 8, 0);
                        printf("Enter amount: ");
                        fgets(buf, sizeof(buf), stdin);
                        send(s, buf, strlen(buf), 0);
                        break;
                case 2: send(s, "WITHDRAW\n", 9, 0);
                        printf("Enter amount: ");
                        fgets(buf, sizeof(buf), stdin);
                        send(s, buf, strlen(buf), 0);
                        break;
                case 3: send(s, "BALANCE\n", 8, 0); break;
                case 4: send(s, "APPLY_LOAN\n", 11, 0); break;
                case 5: send(s, "VIEW\n", 5, 0); break;
                case 6: send(s, "LOGOUT\n", 7, 0); break;
                default: printf("Invalid choice\n"); break;
            }
        } else if (strcmp(role, "EMPLOYEE") == 0) {
            switch (choice) {
                case 1: send(s, "VIEW_CUSTOMER\n", 14, 0); break;
                case 2: send(s, "PROCESS_LOAN\n", 13, 0); break;
                case 3: send(s, "LOGOUT\n", 7, 0); break;
                default: printf("Invalid choice\n"); break;
            }
        } else if (strcmp(role, "MANAGER") == 0) {
            switch (choice) {
                case 1: send(s, "APPROVE_LOAN\n", 13, 0); break;
                case 2: send(s, "VIEW_ALL\n", 9, 0); break;
                case 3: send(s, "LOGOUT\n", 7, 0); break;
                default: printf("Invalid choice\n"); break;
            }
        } else if (strcmp(role, "ADMIN") == 0) {
            switch (choice) {
                case 1: send(s, "ADD_ACCOUNT\n", 12, 0); break;
                case 2: send(s, "DELETE_ACCOUNT\n", 15, 0); break;
                case 3: send(s, "MODIFY_ACCOUNT\n", 15, 0); break;
                case 4: send(s, "SEARCH_ACCOUNT\n", 15, 0); break;
                case 5: send(s, "VIEW_ALL\n", 9, 0); break;
                case 6: send(s, "LOGOUT\n", 7, 0); break;
                default: printf("Invalid choice\n"); break;
            }
        }
    }

    close(s);
    return 0;
}
