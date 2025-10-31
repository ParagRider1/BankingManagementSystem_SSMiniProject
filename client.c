#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080

// helper: send string + newline if not already present
static void send_line(int sock, const char *s) {
    size_t len = strlen(s);
    if (len == 0) return;
    // send s
    send(sock, s, len, 0);
    // ensure newline
    if (s[len-1] != '\n') send(sock, "\n", 1, 0);
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

    char buf[4096];
    char role[64] = "";
    while (1) {
        // read server data (may contain multiple lines)
        ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("Server disconnected.\n");
            break;
        }
        buf[n] = '\0';

        // Process every line separately (handles combined ROLE+MENU etc.)
        char *saveptr = NULL;
        char *line = strtok_r(buf, "\n", &saveptr);
        while (line) {
            // trim leading/trailing spaces
            while (*line == '\r') line++;
            if (strncmp(line, "ROLE:", 5) == 0) {
                // receive ROLE:NAME
                strncpy(role, line + 5, sizeof(role)-1);
                role[sizeof(role)-1] = '\0';
                printf("\nLogged in as %s\n", role);
            }
            else if (strncmp(line, "MENU", 4) == 0 || strncmp(line, "MENU:", 5) == 0) {
                // server is telling client to display menu
                if (strcmp(role, "CUSTOMER") == 0) show_customer_menu();
                else if (strcmp(role, "EMPLOYEE") == 0) show_employee_menu();
                else if (strcmp(role, "MANAGER") == 0) show_manager_menu();
                else if (strcmp(role, "ADMIN") == 0) show_admin_menu();
            }
            else {
                // generic server message
                printf("%s\n", line);
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }

        // If the server printed a menu, we already printed prompt. Otherwise show generic prompt.
        printf("> ");
        fflush(stdout);

        // read user input
        if (!fgets(buf, sizeof(buf), stdin)) break;
        buf[strcspn(buf, "\n")] = 0;
        if (strlen(buf) == 0) {
            // if empty line, send a newline (or skip)
            send_line(s, "");
            continue;
        }

        // If user typed numeric menu choice and role is set, translate to server token.
        int choice = atoi(buf);
        if (role[0] != '\0' && choice > 0) {
            // role-aware mapping
            if (strcmp(role, "CUSTOMER") == 0) {
                switch (choice) {
                    case 1:
                        send_line(s, "DEPOSIT");
                        printf("Enter amount: ");
                        if (!fgets(buf, sizeof(buf), stdin)) break;
                        buf[strcspn(buf, "\n")] = 0;
                        send_line(s, buf);
                        break;
                    case 2:
                        send_line(s, "WITHDRAW");
                        printf("Enter amount: ");
                        if (!fgets(buf, sizeof(buf), stdin)) break;
                        buf[strcspn(buf, "\n")] = 0;
                        send_line(s, buf);
                        break;
                    case 3: send_line(s, "BALANCE"); break;
                    case 4: send_line(s, "APPLY_LOAN"); break;
                    case 5: send_line(s, "VIEW"); break;
                    case 6: send_line(s, "LOGOUT"); break;
                    default: printf("Invalid choice\n"); break;
                }
            } else if (strcmp(role, "EMPLOYEE") == 0) {
                switch (choice) {
                    case 1: send_line(s, "VIEW_PENDING"); break;
                    case 2:
                        send_line(s, "MARK_REVIEW");
                        printf("Enter account id to mark reviewed: ");
                        if (!fgets(buf, sizeof(buf), stdin)) break;
                        buf[strcspn(buf, "\n")] = 0;
                        send_line(s, buf);
                        break;
                    case 3: send_line(s, "LOGOUT"); break;
                    default: printf("Invalid choice\n"); break;
                }
            } else if (strcmp(role, "MANAGER") == 0) {
                switch (choice) {
                    case 1: send_line(s, "LIST_REVIEWED"); break;
                    case 2:
                        send_line(s, "APPROVE");
                        printf("Enter account id to approve: ");
                        if (!fgets(buf, sizeof(buf), stdin)) break;
                        buf[strcspn(buf, "\n")] = 0;
                        send_line(s, buf);
                        break;
                    case 3: send_line(s, "LOGOUT"); break;
                    default: printf("Invalid choice\n"); break;
                }
            } else if (strcmp(role, "ADMIN") == 0) {
                switch (choice) {
                    case 1:
                        send_line(s, "ADD_ACCOUNT");
                        // server will prompt for fields; the client will receive those prompts and then send responses
                        break;
                    case 2: send_line(s, "DELETE_ACCOUNT"); break;
                    case 3: send_line(s, "MODIFY_ACCOUNT"); break;
                    case 4: send_line(s, "SEARCH_ACCOUNT"); break;
                    case 5: send_line(s, "VIEW_ALL"); break;
                    case 6: send_line(s, "LOGOUT"); break;
                    default: printf("Invalid choice\n"); break;
                }
            } else {
                // role unknown, just send raw
                send_line(s, buf);
            }
        } else {
            // Non-numeric input or no role yet: send raw line to server
            send_line(s, buf);
        }
    }

    close(s);
    return 0;
}
