#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "common.h" 


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

// --- UPDATED MENUS TO MATCH SERVER ---

void show_customer_menu() {
    printf("\n====== CUSTOMER MENU ======\n");
    printf("1. Deposit\n");
    printf("2. Withdraw\n");
    printf("3. Balance Enquiry\n");
    printf("4. Apply Loan\n");
    printf("5. View Details\n");
    printf("6. Logout\n");
    printf("============================\nEnter choice: ");
}

void show_employee_menu() {
    printf("\n====== EMPLOYEE MENU ======\n");
    printf("1. View Pending Loans\n");       // Was "View Customer Details"
    printf("2. Mark Loan as Reviewed\n"); // Was "Process Loan Request"
    printf("3. View Specific Account\n"); // New option to match server
    printf("4. Logout\n");
    printf("============================\nEnter choice: ");
}

void show_manager_menu() {
    printf("\n====== MANAGER MENU ======\n");
    printf("1. List Reviewed Loans\n"); // Was "Approve/Reject Loan"
    printf("2. Approve Loan\n");        
    printf("3. Reject Loan\n");        
    printf("4. Logout\n");             
    printf("===========================\nEnter choice: ");
}

void show_admin_menu() {
    printf("\n====== ADMIN MENU ======\n");
    printf("1. Add Account\n");
    printf("2. Delete Account\n");
    printf("3. Modify Account\n");
    printf("4. Search Account\n");
    printf("5. View All Accounts\n");
    printf("6. Logout\n");
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
    char extra_input[128];
    bool is_menu_prompt = false; // <-- State flag to fix Admin menu

    while (1) {
        // --- Read server message ---
        ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            printf("Server disconnected.\n");
            break;
        }
        buf[n] = '\0';
        is_menu_prompt = false; // Reset flag each time we get a message

        if (strncmp(buf, "ROLE:", 5) == 0) {
            char* role_start = buf + 5;
            char* role_end = strpbrk(role_start, "\r\n"); // Find end of role
            if (role_end) {
                *role_end = '\0';
            }
            strcpy(role, role_start);
            printf("\nLogged in as %s\n", role);
     
        }

      
        if (strstr(buf, "MENU") != NULL) {
            is_menu_prompt = true; // Set the flag

            if (strcmp(role, "CUSTOMER") == 0) show_customer_menu();
            else if (strcmp(role, "EMPLOYEE") == 0) show_employee_menu();
            else if (strcmp(role, "MANAGER") == 0) show_manager_menu();
            else if (strcmp(role, "ADMIN") == 0) {
                // Admin menu is sent *by* the server, so just print it.
                printf("%s", buf);
            }
        } else if (strncmp(buf, "ROLE:", 5) != 0) {
           
            printf("%s", buf);
            if (buf[strlen(buf) - 1] != '\n') printf("\n");
        }

        // --- Exit conditions ---
        if (strstr(buf, "Logging out") || strstr(buf, "Connection closed")) break;

        // --- Get user input ---
        printf("> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) break; // Get user choice, e.g., "1\n"

        // --- CLIENT LOGIC ---

        // If we are not logged in, just send the raw buffer (for role, user, pass)
        if (strlen(role) == 0) {
            send(s, buf, strlen(buf), 0);
            continue;
        }

   
        if (strcmp(role, "ADMIN") == 0 && !is_menu_prompt) {
            send(s, buf, strlen(buf), 0);
            continue; // Go back to recv()
        }

        // --- Original Menu Choice Logic ---
        // If we are here, it means we are logged in AND it's a menu prompt
        
        int choice = atoi(buf);

        if (strcmp(role, "CUSTOMER") == 0) {
            switch (choice) {
                case 1: // Deposit
                    send(s, "DEPOSIT\n", 8, 0);
                    printf("Enter amount: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 2: // Withdraw
                    send(s, "WITHDRAW\n", 9, 0);
                    printf("Enter amount: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 3: send(s, "BALANCE\n", 8, 0); break;
                case 4: send(s, "APPLY_LOAN\n", 11, 0); break;
                case 5: send(s, "VIEW\n", 5, 0); break;
                case 6: send(s, "LOGOUT\n", 7, 0); break;
                default: send(s, "INVALID\n", 8, 0); break;
            }
        } else if (strcmp(role, "EMPLOYEE") == 0) {
            switch (choice) {
                case 1: send(s, "VIEW_PENDING\n", 13, 0); break;
                case 2: 
                    send(s, "MARK_REVIEW\n", 12, 0);
                    printf("Enter Account ID to mark as reviewed: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 3:
                    send(s, "VIEW_ACCOUNT\n", 13, 0);
                    printf("Enter Account ID to view: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 4: send(s, "LOGOUT\n", 7, 0); break;
                default: send(s, "INVALID\n", 8, 0); break;
            }
        } else if (strcmp(role, "MANAGER") == 0) {
            switch (choice) {
                case 1: send(s, "LIST_REVIEWED\n", 14, 0); break;
                case 2:
                    send(s, "APPROVE\n", 8, 0);
                    printf("Enter Account ID to approve: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 3:
                    send(s, "REJECT\n", 7, 0);
                    printf("Enter Account ID to reject: ");
                    fgets(extra_input, sizeof(extra_input), stdin);
                    send(s, extra_input, strlen(extra_input), 0);
                    break;
                case 4: send(s, "LOGOUT\n", 7, 0); break;
                default: send(s, "INVALID\n", 8, 0); break;
            }
        } else if (strcmp(role, "ADMIN") == 0) {
            // This is now ONLY for the main menu choice
            switch (choice) {
                case 1: send(s, "ADD_ACCOUNT\n", 12, 0); break;
                case 2: send(s, "DELETE_ACCOUNT\n", 15, 0); break;
                case 3: send(s, "MODIFY_ACCOUNT\n", 15, 0); break;
                case 4: send(s, "SEARCH_ACCOUNT\n", 15, 0); break;
                case 5: send(s, "VIEW_ALL\n", 9, 0); break;
                case 6: send(s, "LOGOUT\n", 7, 0); break;
                default: send(s, "INVALID\n", 8, 0); break;
            }
        }
    } // end while

    close(s);
    return 0;
}
