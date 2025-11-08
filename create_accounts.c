#include <stdio.h>
#include <string.h>
#include "common.h" 



int main() {
    
    FILE *fp = fopen(DB_ACC_FILE, "wb"); 
    if (!fp) {
       
        perror("Failed to create " DB_ACC_FILE); 
        return 1;
    }

    Account users[] = {
        {1, "cust101", "pass101", "CUSTOMER", 1500.0, 0},
        {2, "cust102", "pass102", "CUSTOMER", 3000.0, 1}, // loan_pending = 1 (Pending)
        {3, "emp201", "emp201", "EMPLOYEE", 0.0, 0},
        {4, "mgr301", "mgr301", "MANAGER", 0.0, 0},
        {5, "admin123", "1234", "ADMIN", 0.0, 0}
        // This all works with the Account struct in common.h 
    };

    fwrite(users, sizeof(Account), sizeof(users) / sizeof(Account), fp);
    fclose(fp);

   
    printf("%s created successfully with %zu users.\n", DB_ACC_FILE,
           sizeof(users) / sizeof(Account));

    return 0;
}
