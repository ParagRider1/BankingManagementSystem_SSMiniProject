# 💳 Multi-User Banking Management System (Socket + Threads)

A **multi-threaded, role-based banking system** built in C using **TCP sockets** and **POSIX threads**.  
It enables real-time concurrent banking operations with **Admin, Manager, Employee, and Customer** roles, featuring full CRUD operations, secure file access, and loan processing.

---

## 🏗️ Quick Start

### 🔧 Compile and Run

```bash
# Compile the server
gcc server.c -o server -lpthread

# Compile the client
gcc client.c -o client
```

### ▶️ Run the System
```bash
# Start the server
./server

# Start a client
./client
```

### 🧾 Default Admin Login
| Field | Value |
|--------|--------|
| Username | `admin123` |
| Password | `1234` |

---

## 🧠 System Overview

### ⚙️ Architecture
```
+------------------+           +------------------+
|      Client      |  <---->   |      Server      |
|------------------|           |------------------|
| Socket (TCP/IP)  |           | Multi-threaded   |
| Interactive Menu |           | Role Handlers    |
+------------------+           +------------------+
          ↕                        ↕
      accounts.dat (Shared Database File)
```

### 📘 Core Components

| File | Description |
|------|--------------|
| **server.c** | Multi-threaded backend, handles role logic and file I/O |
| **client.c** | User interface for menu-driven interactions |
| **common.h** | Common struct definitions (`Account`) |
| **accounts.dat** | Binary database storing all account details |

---

## 👥 Role-Based Functionalities

### 🧑‍💼 **Admin**
- Add / Delete / Modify / Search accounts  
- View all accounts  
- Manage all user roles  
- Ensure data integrity and synchronization  

### 👨‍🔧 **Employee**
- View customer details  
- Process pending loan requests  
- View assigned customers and update records  
- Logout after session completion  

### 👩‍💼 **Manager**
- Approve or reject loans reviewed by employees  
- View all active accounts  
- Manage employees and customers  
- Maintain consistency in loan handling  

### 👨‍👩‍💻 **Customer**
- Deposit / Withdraw funds  
- Check account balance  
- Apply for loans  
- View loan and account details  

---

## 🧮 Menus (Displayed in Client)

### **Customer Menu**
```
1. Deposit
2. Withdraw
3. Balance Enquiry
4. Apply Loan
5. View Details
6. Logout
```

### **Employee Menu**
```
1. View Customer Details
2. Process Loan Request
3. Logout
```

### **Manager Menu**
```
1. Approve/Reject Loan
2. View All Accounts
3. Logout
```

### **Admin Menu**
```
1. Add Account
2. Delete Account
3. Modify Account
4. Search Account
5. View All Accounts
6. Logout
```

---

## 🧾 Data Model

```c
typedef struct {
    int id;
    char username[50];
    char password[50];
    char role[20];       // CUSTOMER / EMPLOYEE / MANAGER / ADMIN
    float balance;
    int loan_pending;    // 0=None, 1=Requested, 2=Reviewed, 3=Approved
} Account;
```

---

## 🧠 System Flow

### 🔹 Login Flow
```
Client → Role Selection → Username & Password → Authentication (Server)
↓
Role-based handler executes operations
```

### 🔹 Loan Processing Flow
```
Customer: APPLY_LOAN
↓
Employee: MARK_REVIEW
↓
Manager: APPROVE / REJECT
↓
Balance updated automatically
```

---

## 🧩 Architecture Blueprint

A detailed **UML-style class diagram** showing relationships among modules, user roles, and database entities.

![Banking Management System Blueprint](./banking%20management%20blueprint%20.png)

---

## 🔒 Concurrency & Synchronization

- Thread-safe file updates using **`pthread_mutex`**  
- File-level access protection via **`flock()`**  
- Independent client sessions  
- Role-based command handling  
- Controlled access to `accounts.dat`  

---

## 🧰 Tech Stack

| Component | Technology |
|------------|-------------|
| Language | C |
| Networking | TCP sockets |
| Concurrency | POSIX threads |
| Synchronization | Mutex + File locks |
| Persistence | Binary File (accounts.dat) |
| Platform | Linux / Unix |

---

## ⚡ Future Enhancements
- Transaction history logging  
- Password encryption  
- Feedback system  
- Manager analytics dashboard  
- SQLite / PostgreSQL backend  
- Web dashboard with REST API  

---

## 👨‍💻 Developer Notes

- Use **multiple terminal clients** to test concurrency  
- Ensure `accounts.dat` or `accounts.db` exists before login  
- Run on **Linux** or **WSL** for best performance  
- Supports **simultaneous multi-role logins**

---

## 🧾 License
Released under the **MIT License**

---

## ✨ Contributors
**Parag Piprewar**  
*M.Tech CSE @ IIIT Bangalore*  
Focused on distributed systems, concurrency, and secure backend design.
