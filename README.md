# 🚀 Topic-Based Messaging System

This project is a **TCP-based messaging server** that enables clients to **connect, subscribe to topics, publish messages, and disconnect**. The server efficiently routes messages based on **topics**, facilitating communication between applications or users.

---

## 📌 Features

- **Topic-based messaging**: Clients can subscribe to topics and receive only relevant messages.
- **Efficient message routing**: The server ensures that messages are delivered only to subscribed clients.
- **Multiple client support**: Multiple clients can connect, publish, and receive messages simultaneously.
- **Lightweight and fast**: Designed for minimal latency and efficient communication.
- **Simple command structure**: Easy-to-use text-based commands.

---

## 🛠 Usage

### **Starting the Server**
Run the server, specifying the port number where it will listen for client connections:

```bash
# If port is not specified it will listen on port 1999
./build/topic-server -l <port>
```

### **Starting the Client**
Run the client and connect it to the server:

```bash
# For remote server
./build/topic-client -s <server_ip> -p <port> -n <clientName>

# or for localhost
./build/topic-client -p <port> -n <clientName>

# or without arguments, in that case use internal CONNECT command
./build/topic-client
```

---

## 📌 Server Functionality

The **server application** listens for incoming connections and manages subscriptions. It logs:

- **Client connections/disconnections** (`CONNECT`/`DISCONNECT`)
- **Published messages** (`PUBLISH`)
- **Topic subscriptions** (`SUBSCRIBE`/`UNSUBSCRIBE`)

The server maintains a **subscription registry**, ensuring messages are only sent to subscribed clients.

---

## 📌 Client Commands

| **Command**                         | **Description**                                    |
| ----------------------------------- | -------------------------------------------------- |
| `CONNECT <port> <client_name>`      | Connects to the server with the given client name. |
| `CONNECT <ip> <port> <client_name>` | Connects to the server with the given client name. |
| `DISCONNECT`                        | Disconnects from the server.                       |
| `PUBLISH <topic> <message>`         | Publishes a message to a topic.                    |
| `SUBSCRIBE <topic>`                 | Subscribes to receive messages from a topic.       |
| `UNSUBSCRIBE <topic>`               | Unsubscribes from a topic.                         |

### **Receiving Messages**
When a client receives a message from a **subscribed topic**, it is printed in the following format:

```
[Message] Topic: <topic> Data: <message>
```

Example:
```
[Message] Topic: sports Data: "Team A won the match!"
```

---

## 📌 Assumptions

- **Topic names** are in **ASCII format** and do not contain spaces.
- **Messages** are plain ASCII text.
- **Messages are delimited** using a predefined separator at the TCP/IP level.

---

## 🔧 Build Instructions

### **Installing Dependencies (Ubuntu)**

```bash
sudo apt update && sudo apt install -y \
    g++ \
    make \
    libstdc++6 \
    libboost-all-dev \
    build-essential
```

### **Updating Third-Party Libraries**

```bash
git submodule update --init --recursive
```

### **Building the Project**

```bash
make all       # Compile both server and client
make server    # Compile only the server
make client    # Compile only the client
```

---

## 🚀 Example Usage

### **1. Start the Server**
```bash
# If port is not specified it will listen on port 1999
./build/topic-server 

# With specified port 
./build/topic-server -l 27374

```



### **2. Start a Client and Connect to Server**
Examples:
```bash
# For remote server running on 192.168.0.10 and listening on port 27374
./build/topic-client -s 192.168.0.10 -p 27374 -n Neo

# or if server is running on local host and listening on port 27374
./build/topic-client -p 27374 -n Morpheus

# or without arguments, in that case use internal CONNECT command
./build/topic-client
```

### **2a. Subscribe to a Topic**
```sh
CONNECT 1999 Alice
```

### **3. Subscribe to a Topic**
```sh
SUBSCRIBE news
```

### **4. Publish a Message**
```sh
PUBLISH news "Breaking News: Major update released!"
```

### **5. Receive the Message (On Subscribed Client)**
```
[Message] Topic: news Data: "Breaking News: Major update released!"
```

---

## 🛠 Troubleshooting

- **Client fails to connect?**
  - Ensure the server is running and the correct IP/port is used.
  - Check firewall settings.

- **Messages not received?**
  - Verify the client is subscribed to the correct topic.

- **Build issues?**
  - Ensure all dependencies are installed.
  - Try running `make clean` before `make all`.

---

## 📜 License

This project is licensed under the MIT License.

